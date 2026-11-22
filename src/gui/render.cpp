#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "canvas_sink.h"    // the canvas painters' one road (GuiCanvasSink)
#include "gui_display_context.h"
#include "gui_font.h"
#include "text_shape.h"
#include "text_raster.h"    // XSpan, the cut labels' visible intervals
#include "palette_file.h"   // kGuiPaletteFill (the palette off the row)
#include "cool_edit_paint.h"   // the canvas column's painters and constants
#include "materials.h"
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
// head's painted reach, cue_chip_paint_reach_px (render.h, the chip's), the
// one extent both read.

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
    set_palette_source(cr, palette().body_face);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_restore(cr);
}

namespace {
// ONE CANVAS LINE ACROSS THE AREA AT ITS EXACT ROWS (architect 2026-11-14,
// send J; the horizontals' one row rule, render.h's canvas_hline_span): the
// line one W thick centered on the real window row `yc`, cut to the area's
// rows, as ONE BLENDED RECT at 100 percent across the area's whole columns
// — the word itself on every row it covers whole, the word over what lies
// beneath by the row's coverage where its edge crosses a row; whole rows at
// both edges are an opaque rect's bytes. The grid's and the center lines'
// one form (render_canvas, render_center_lines); an empty span paints
// nothing (the sink's rule).
void canvas_hline(GuiCanvasSink& sink, const GuiRect& area, double yc,
                  GuiColor color) {
    const CanvasRowSpan rows = canvas_hline_span(area, yc);
    sink.blended_rect(static_cast<double>(area.x), rows.y0,
                      static_cast<double>(area.x + area.w), rows.y1, color,
                      100);
}
} // namespace

namespace {
// THE GRID across `area` (its columns; its rows the canvas's), in `color`.
void canvas_grid_lines(GuiCanvasSink& sink, const GuiRect& area,
                       GuiColor color) {
    // THE LINES UNDER THE WAVEFORM, in render.h's row-6 canvas order: the
    // horizontal grid — t = waveform_line_px() thick, ONE W (2 device px on
    // the tablet at 200 %, the unit, architect 2026-11-04), each inside
    // its band. THE CENTER LINES STAND OVER THE WAVEFORM
    // (architect 2026-10-09 evening), render_center_lines below, a pass after
    // the plate. NO VERTICAL GRID (architect 2026-10-09 ~18:40, the
    // rule and its reason at render.h's row-6 canvas paragraph).
    // THE HORIZONTAL GRID per channel, on the plate's own bands
    // (waveform_channel_band), AT ITS EXACT ROWS (architect 2026-11-14, send
    // J): the k-th line pair stands k · H / n rows either side of THE BAND'S
    // EXACT ZERO — the plate's own centre, WaveformChannelBand::zero — H the
    // channel's half height h / 2.0 and n the grid's divisions, k = 1 ..
    // n − 1 (the n-th on the band's edge not drawn), each line centered on
    // that real row (canvas_hline), so the lines go soft where the band's
    // division falls between rows: on a 525-row band, 65.625, 131.25 and
    // 196.875 rows from a zero on a row's
    // middle. Each band is its well's interior (no inset, architect
    // 2026-10-09; the two wells 2026-10-16), so H is half a band, every
    // line inside it.
    const WaveformDivider div = waveform_divider(area.h);
    for (int ch = 0; ch < 2; ++ch) {
        const WaveformChannelBand band =
            waveform_channel_band(area.h, div.h(), ch);
        if (band.h <= 0) continue;
        const double zero = static_cast<double>(area.y) + band.zero();
        const double half = static_cast<double>(band.h) / 2.0;
        const int n = kProgramSpec.grid_divisions;
        for (int k = 1; k < n; ++k) {
            const double d =
                static_cast<double>(k) * half / static_cast<double>(n);
            canvas_hline(sink, area, zero - d, color);
            canvas_hline(sink, area, zero + d, color);
        }
    }
}
// THE CENTER LINES across `area`, the same way.
void canvas_center_lines(GuiCanvasSink& sink, const GuiRect& area,
                         GuiColor color) {
    // THE CENTER LINE per channel ON ITS BAND'S EXACT ZERO, OVER THE
    // WAVEFORM'S INK (architect 2026-10-09 evening: "it's supposed to be
    // visible over the waveform. Otherwise, in a project like this with tape
    // hiss, there's never enough zero that the red line would become visible
    // ever"; the order at render.h's row-6 canvas paragraph): one W, the
    // grid's row rule (canvas_hline), in the palette's `center`, the
    // channels' bands the plate's own (waveform_channel_band), so it is
    // TRULY CENTERED ON THE ZERO THE PLATE'S BARS STRADDLE (architect
    // 2026-11-14, send J): on an odd band half a row, a whole row and half
    // a row; on an even band (the tablet's 526 rows, the key 28 W tall,
    // send Z) two whole rows. Through the canvas sink (2026-10-13).
    const int divider_px = waveform_divider_px();
    for (int ch = 0; ch < 2; ++ch) {
        const WaveformChannelBand band =
            waveform_channel_band(area.h, divider_px, ch);
        if (band.h > 0)
            canvas_hline(sink, area,
                         static_cast<double>(area.y) + band.zero(), color);
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
// the lines' blended rects as list records. The stereo divider's row is
// render_waveform_divider's, a pass of its own over the plate (below).
void render_canvas(GuiCanvasSink& sink, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    const GuiPalette& pal = palette();
    // The ground is the `waveform_canvas` role (the palette's row-6 block),
    // display_color applied by the sink.
    sink.rects(std::span<const GuiRect>(&area, 1), pal.waveform_canvas);
    canvas_grid_lines(sink, area, pal.grid);
}

void render_center_lines(GuiCanvasSink& sink, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    canvas_center_lines(sink, area, palette().center);
}

// THE STEREO DIVIDER (architect 2026-10-16, Cool Edit's two wells; flat
// since 2026-11-01 night; the rule and its row at render.h's
// waveform_divider, the one geometry owner): across the area's columns, ONE
// LINE OF THE WELL'S LINES' TONE (the palette's `well_border`; architect
// 2026-11-17, session 140, D2) cut to the area's rows —
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
    sink.rects(std::span<const GuiRect>(&r, 1), palette().well_border);
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

// THE EXACT CHROME FILL (the contract is at the declarations, render.h):
// cairo's gray coverage for this one fill, the context's own mode put back.
void paint_cell_rect(cairo_t* cr, const GuiRectF& r, GuiColor c) {
    if (!(r.w > 0.0) || !(r.h > 0.0)) return;
    const cairo_antialias_t was = cairo_get_antialias(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    set_palette_source(cr, c);
    cairo_fill(cr);
    cairo_set_antialias(cr, was);
}

void paint_ink_rect(cairo_t* cr, const GuiRectF& r, GuiColor c) {
    if (!(r.w > 0.0) || !(r.h > 0.0)) return;
    const cairo_antialias_t was = cairo_get_antialias(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    set_palette_source(cr, c);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
    cairo_set_antialias(cr, was);
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

// THE SCALE (render.h's scrub block, Arc's plain scale; architect
// 2026-11-18 / 19, send Z's design, built in send AA): one coverage-stamp
// pass at exact coverage — the trough's pill in `scale_trough`; the played
// part, the same pill cut at the knob's center, in the accent; the knob's
// disc — at rest `key_face` inside a one-W `scale_slider_border` rim,
// hovered its fill `key_hover_face`, held the whole disc the accent — AS
// PAINTED, `knob` its FaceMix (Rest, Hover and Lit; architect 2026-11-20):
// the fill and the rim each the mix of their looks' tones (the stamp's
// tone); the knob wholly held is the one accent disc, byte for byte the
// held picture of before.
void paint_scale(cairo_t* cr, const GuiRect& item, double knob_x,
                 const FaceMix& knob) {
    if (item.w <= 0 || item.h <= 0) return;
    const GuiPalette& p = palette();
    const GuiRectF tr   = scale_trough_box(item);
    const GuiRectF kn   = scale_knob_box(item, knob_x);
    const double tr_r   = tr.h / 2.0;
    const double kn_r   = kn.w / 2.0;
    const double b      = static_cast<double>(hairline_px());
    CoverageStampPass pass;
    pass.begin(cr);
    pass.rounded_rect(tr.x, tr.y, tr.x + tr.w, tr.y + tr.h, tr_r, tr_r,
                      p.scale_trough);
    if (knob_x > tr.x)
        pass.rounded_rect_cut(tr.x, tr.y, tr.x + tr.w, tr.y + tr.h, tr_r, tr_r,
                              tr.x, tr.y, std::min(knob_x, tr.x + tr.w),
                              tr.y + tr.h, p.cue);
    if (knob == FaceMix::of(FaceLook::Lit)) {
        pass.rounded_rect(kn.x, kn.y, kn.x + kn.w, kn.y + kn.h, kn_r, kn_r,
                          p.cue);
    } else {
        const double in_r = std::max(0.0, kn_r - b);
        pass.rounded_rect(kn.x + b, kn.y + b, kn.x + kn.w - b, kn.y + kn.h - b,
                          in_r, in_r,
                          mix_looks(knob, [&](FaceLook l) {
                              return l == FaceLook::Lit     ? p.cue
                                   : l == FaceLook::Hover   ? p.key_hover_face
                                                            : p.key_face;
                          }));
        pass.rounded_ring(kn.x, kn.y, kn.x + kn.w, kn.y + kn.h, kn_r, b,
                          mix_looks(knob, [&](FaceLook l) {
                              return l == FaceLook::Lit ? p.cue
                                                        : p.scale_slider_border;
                          }));
    }
    pass.end();
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

    // THE BASIS'S LATTICE (see WaveformBasis: the plate's caller hands THE
    // BAR BASIS, the frame-0 lattice of step b, its start the left edge of
    // the call's first bar — the first stored bar's, or with the kept bars
    // the first a kept row lacks — architect 2026-11-14, send G; 2026-11-15,
    // send L). Recover the start's
    // lattice index on ITS OWN LATTICE, of origin basis.origin, with the SAME
    // expression clamp_viewport_start uses to snap a viewport onto its own
    // (nearest_viewport_grid_point), so a start on the lattice round-trips
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

    // THE VISUAL MAGNIFICATION, a function of source time, its scale
    // constant over a WORKING COLUMN: the column's OUTER scale is the derived
    // curve's gain at the column's centre frame, its INNER scale the
    // compressor's scale times the foreground gain's half at the same frame
    // (folded together at the derivation), both times the column's own
    // expander multiplier (waveform_column_scales). The contract (the two
    // bars' order and inks, THE EXACT FORM: a bar the hull of its working
    // columns' magnified samples) is at this function's declaration; the
    // arithmetic is one multiply and ONE clamp per tip a piece.
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
    // `waveform_grow` key, four subcells by default) makes a line of — 1 +
    // 2 · GROW subrows, at the default nine: nonzero coverage over three
    // device rows, two and a quarter device px of summed coverage (36 of 16
    // subcells) whichever quarter-row phase the tip has (the numbers' owner
    // kWaveformFloorSubcells, render.h) — so flat or silent material draws a
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
    // the band; the GPU's grow then makes a line of it (1 + 2 · GROW
    // subrows at any tip phase; the numbers at kWaveformFloorSubcells,
    // render.h).
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
    // renders a whole row of bars on its basis — the plate's, or with the
    // kept bars the bars a kept row lacks (WaveformKeptBars, render.h;
    // architect 2026-11-15, send L) — so this call's columns ARE its row;
    // the neighbors a column's picture reads are the GPU's business.
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

        // LIT: THE EXACT FORM (architect 2026-11-15, send O; the definition
        // and the pieces at this function's declaration): the bar's tips are
        // the least and the greatest magnified sample over [s0, s1) clamped
        // to the source, each working column at its own two scales
        // (waveform_column_scales: THE OUTER the gain at the column's centre
        // times its expander multiplier, THE INNER the inner scale — the
        // compressor's times the foreground gain's half — at the same centre
        // times the same multiplier). A bar inside one working column is the
        // full span's one read times that column's scales; across several, a
        // partial column at either end is a piece read directly (at the level
        // of its own width, a cost choice as every level is) at its column's
        // scales and every whole column between is ONE QUERY of THE LIT
        // SUMMARY, the tips the pieces' hull; the full span is not read.
        // Each tip is clamped to the
        // sample domain [-1, 1] BEFORE it becomes rows: the clamp is what
        // makes a magnified forte clip flat against the lane's edges instead
        // of running off into row arithmetic (the inner's scale, c x 1/2, is
        // at most 1/2, so its clamp is a no-op kept for the one shape). A
        // PICTURE gain: the samples themselves are untouched, here and
        // everywhere.
        // DARK: the full span's one read, the raw bar at scale 1.0, where the
        // clamp is a no-op (raw peaks already rest in range).
        if (gain_or_null) {
            const WaveformGainCurve& curve = *gain_or_null;
            const int64_t col   = curve.column_frames;
            const int64_t total = audio.total_frames();
            // A curve with no column is a zero-frame source's, which never
            // reaches here (num_levels() is 0 there, refused above).
            assert(col >= 1);
            const int64_t a = std::max<int64_t>(s0, 0);
            const int64_t b = std::min<int64_t>(s1, total);
            double olo = 0.0, ohi = 0.0, ilo = 0.0, ihi = 0.0;
            if (a < b) {
                const int64_t ka = a / col;
                const int64_t kb = (b - 1) / col;
                if (ka == kb) {
                    const int  level = level_for_column(g1 - g0);
                    const auto mm = audio.get_peak_range(channel, level, s0, s1);
                    const WaveformColumnScales sc = waveform_column_scales(curve, ka);
                    olo = magnified_tip(mm.first,  sc.outer);
                    ohi = magnified_tip(mm.second, sc.outer);
                    ilo = magnified_tip(mm.first,  sc.inner);
                    ihi = magnified_tip(mm.second, sc.inner);
                } else {
                    olo = ilo =  std::numeric_limits<double>::infinity();
                    ohi = ihi = -std::numeric_limits<double>::infinity();
                    const auto piece = [&](int64_t p0, int64_t p1, int64_t k) {
                        const auto pm = audio.get_peak_range(
                            channel, audio.level_for_span(static_cast<double>(p1 - p0)),
                            p0, p1);
                        const WaveformColumnScales sc = waveform_column_scales(curve, k);
                        olo = std::min(olo, magnified_tip(pm.first,  sc.outer));
                        ohi = std::max(ohi, magnified_tip(pm.second, sc.outer));
                        ilo = std::min(ilo, magnified_tip(pm.first,  sc.inner));
                        ihi = std::max(ihi, magnified_tip(pm.second, sc.inner));
                    };
                    int64_t wa = ka;
                    int64_t wb = kb;
                    if (a != ka * col) {
                        piece(a, (ka + 1) * col, ka);
                        wa = ka + 1;
                    }
                    if (b != std::min((kb + 1) * col, total)) {
                        piece(kb * col, b, kb);
                        wb = kb - 1;
                    }
                    if (wa <= wb) {
                        const WaveformLitHull h =
                            waveform_lit_summary_hull(curve, channel, wa, wb);
                        olo = std::min(olo, h.outer_min);
                        ohi = std::max(ohi, h.outer_max);
                        ilo = std::min(ilo, h.inner_min);
                        ihi = std::max(ihi, h.inner_max);
                    }
                }
            }
            outer_tips[static_cast<size_t>(i)] = bar_tips(olo, ohi);
            inner_tips[static_cast<size_t>(i)] = bar_tips(ilo, ihi);
        } else {
            const int  level = level_for_column(g1 - g0);
            const auto mm = audio.get_peak_range(channel, level, s0, s1);
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
// ONE SOLID CANVAS LINE DOWN THE AREA AT ITS EXACT PLACE (architect
// 2026-11-14, send J; the rule at render.h's waveform_line_span): the
// line's real span of columns (`span`, the caller's — the line's real
// interval cut to the area's, empty where none of it is inside) as ONE
// BLENDED RECT at 100 percent over
// the area's whole height, the stereo divider's row crossed (architect
// 2026-11-02, render.h's waveform_divider) — the cursor's, the scanner's
// and the zoom anchor's one form, one sink record; an empty span paints
// nothing (the sink's rule).
void canvas_line(GuiCanvasSink& sink, const GuiRect& area,
                 const WaveformLineSpan& span, GuiColor color) {
    sink.blended_rect(span.x0, static_cast<double>(area.y), span.x1,
                      static_cast<double>(area.y + area.h), color, 100);
}
} // namespace

void render_playhead(GuiCanvasSink& sink,
                     GuiRect area,
                     int     col,
                     double  x,
                     GuiColor color) {
    if (area.w <= 0 || area.h <= 0) return;
    // Allow partial render at file start / end: a playhead whose column has
    // clipped just past the area edge still gets here, and the line's own
    // span below decides whether any pixel lands. This keeps the playhead's
    // visual center aligned with its true frame position rather than
    // snapping it inward at the rightmost samples. THE CULL, on the whole
    // column: the bound is the head's painted reach
    // (cue_chip_paint_reach_px, render.h), stated in the same columns the
    // playhead's damage box takes (playhead_invalidate_rect, main.cpp) — wider than
    // the line by construction (the reach is the chip's height and one, the
    // line reaches half a W from a place within half a px of the column),
    // so it never drops a line whose real interval reaches the area — which
    // the scanner's damage stands on: its rect is the hull of the span below
    // (scanner_invalidate_rect, main.cpp), every pixel this function can
    // touch at that x.
    if (col + cue_chip_paint_reach_px() <= 0) return;
    if (col - cue_chip_paint_reach_px() >= area.w) return;

    // THE LINE STANDS AT THE FRAME'S EXACT PLACE (architect 2026-11-14, send
    // J): `x` is the caller's playhead_exact_x / scanner_exact_x,
    // displayed_x_at on the plate basis — the plate's own continuous column
    // coordinate, so the line moves with the plate under a pan and a zoom
    // by the same real amount and stands where its frame lies among the
    // bars: a bar [k·b, (k+1)·b) stands over its own frames, and the line
    // is centered on its frame's boundary inside it — no rounding to a
    // lattice point stands between the line and its frame (the half-column
    // bias such a rounding carries, accepted 2026-09-02 as a coarse-zoom
    // cost, is the column's alone). THE COLUMN IS THE CULL'S AND THE
    // PLAYHEAD'S DAMAGE BOX'S:
    // `col` is the caller's playhead_pixel_x / scanner_pixel_x,
    // displayed_column_at's K − m on the same basis (architect 2026-11-06,
    // session 68; a tie TO THE RIGHT, session 69: index K owns [K − 0.5,
    // K + 0.5), the reason at displayed_column_at) — the column the ruler's
    // head's cull and hit and the playhead's damage box are built on
    // (playhead_invalidate_rect), within half a px of x; the scanner's
    // damage is built on x itself (scanner_invalidate_rect).
    // THE POINT MODEL STAYS LOAD-BEARING FOR EVERY LANDING: every
    // column→frame landing in the product rides the lattice point,
    // with the worst-case round-trip residue of the target-view landing's
    // derivation (half a source frame at the 1/16 slope floor plus three half
    // target frames, 9.5 / q px at the deepest zoom's q, half the working
    // column): 0.413 px on the tablet (q = 23), and under half a pixel at
    // every waveform width, the
    // deepest-zoom floor holding q >= 20 (0.475 px; kDeepestZoomMinFramesPerPx,
    // app_state.h). Reading the bar as [g(c) − spp/2, g(c) + spp/2) is the
    // alternative that was declined.

    // The waveform_line_px()-wide line (render.h), centered on its frame's
    // exact place, paints whenever any of its real interval is inside the
    // area, cut at both edges (waveform_line_span; architect 2026-11-15,
    // the no-grid arc), so it never leaks into an adjacent region, over
    // whatever it crosses — waveform ink included. THE CANVAS'S WHOLE HEIGHT, the stereo
    // divider's row crossed (architect 2026-11-02, render.h's
    // waveform_divider): solid, the scanner and the resting cursor alike
    // (2026-11-04), one blended rect.
    canvas_line(sink, area, waveform_line_span(area.x, area.w, x), color);
}

void render_hover_line(GuiCanvasSink& sink, GuiRect area, double x,
                       GuiColor color) {
    if (area.w <= 0 || area.h <= 0) return;
    // The line's own span (drawn where any of it is inside the strip, cut at
    // both edges), the canvas's whole height — the playhead's line's form
    // (the rule at the declaration).
    canvas_line(sink, area, waveform_line_span(area.x, area.w, x), color);
}

void render_strip_anchor_stem(GuiCanvasSink& sink, GuiRect area, double x) {
    if (area.w <= 0 || area.h <= 0) return;
    // The clamp is where the affordance lives: an anchor pushed to (or past) a
    // song edge pins to the edge column's place (the producer's clamp,
    // strip_anchor_stem_x), so the stem draws exactly there.
    // The line is waveform_line_px() wide (render.h), centered on its exact
    // place and cut at both edges (waveform_line_span), so an anchor
    // pinned to the first or the last column paints its columns inside.

    // THE ANCHOR STEM IS THE SCANNER'S WHITE (architect 2026-10-09: "the zoom
    // stem and the scanner are both white, which unifies them: the
    // non-playhead stems that are related to the controls should be white,
    // and then the playhead is yellow"): a control's line during a gesture,
    // as the scanner is playback's, in the one `scanner` role — the playhead
    // wears its own two roles (render.h's playhead paragraph).
    // The canvas's whole height (2026-10-09: no line crosses the row of
    // ground above the canvas; the stereo divider's row it crosses, architect
    // 2026-11-02).
    canvas_line(sink, area, waveform_line_span(area.x, area.w, x),
                palette().scanner);
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
    // The painters' q, given back by the span over wave_w
    // (recovered_painter_spp, warp_frame_map_view.h: vp_end is
    // vp_start + nearbyint(wave_w·q), and the recovery is q exactly).
    const double samples_per_pixel =
        recovered_painter_spp(static_cast<int64_t>(vp_end - vp_start), wave_w);
    // THE EXACT PLACE on the caller's UNIFIED displayed basis (this file's
    // header block above): displayed_x_at, warp_frame_map_view.h.
    out.x_raw = displayed_x_at(displayed_ms, static_cast<double>(vp_start),
                               samples_per_pixel, lattice_origin);
    // THE IN-VIEW VERDICT (render.h): the left edge the viewport's named
    // start, the right edge the strip's real right edge, x = wave_w — never
    // the rounded vp_end, which at a fractional rest can stand a frame short
    // of the strip's edge and put a visible last frame out of view.
    out.in_viewport = displayed_ms >= static_cast<double>(vp_start) &&
                      out.x_raw < static_cast<double>(wave_w);
    // THE EOF-WALL CLAMP, an IN-VIEW bound's alone (render.h): its place can
    // stand past the last column's left edge, and the clamp keeps its end
    // zone whole. An offscreen bound keeps its own place, so its zone stands
    // where the bound is and the lane's clip cuts it (architect 2026-10-05).
    out.x = out.x_raw;
    if (out.in_viewport && wave_w > 0)
        out.x = std::clamp(out.x, 0.0, static_cast<double>(wave_w - 1));
    return out;
}

TrimBridgeGap trim_bridge_gap(const TrimBoundColumn& begin,
                              const TrimBoundColumn& end, double inside) {
    // Contract at the declaration: the gap runs between the two zones'
    // inner edges, wherever the zones stand — on screen, part on it, or
    // past either edge (architect 2026-10-05).
    TrimBridgeGap g;
    g.lo = begin.x + inside;
    g.hi = end.x + 1.0 - inside;
    return g;
}

TrimSpanColumns trim_span_columns(const TrimBoundColumn& begin,
                                  const TrimBoundColumn& end, int wave_w) {
    // Contract at the declaration: the bounds' exact places, unclamped (the
    // slider's box, the pill's own), the end's one px past its own, cut to
    // the effective width.
    TrimSpanColumns s;
    s.lo = std::max(begin.x_raw, 0.0);
    s.hi = std::min(end.x_raw + 1.0, static_cast<double>(wave_w));
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

GuiRectF trim_endcap_rect(bool is_begin, int strip_x,
                          const TrimBoundColumn& begin,
                          const TrimBoundColumn& end, GuiRect row) {
    // Contract at the declaration (THE TRIM BAR'S CAPS AND GRABS, render.h):
    // each zone reaches `outside` past its bound and `inside` into the span
    // — on the tablet 4 and 24 device px — and where the two inside reaches
    // cross the zones split at the midpoint between the bounds, each keeping
    // its outside reach. THE RULE READS THE PLACES WHEREVER THEY ARE, on
    // screen or off (architect 2026-10-05): a window panned past an edge
    // keeps its zones and slides off with them. The rect is the ZONE over
    // the trim lane `row`'s whole height — the hit band, before the lane's
    // clip.
    const double outside = trim_grab_outside_px();
    const double inside  = trim_grab_inside_px();
    const double sx = static_cast<double>(strip_x);
    const double b_lo = sx + begin.x;         // the begin bound, the span's left end
    const double e_hi = sx + end.x + 1.0;     // the end bound, its right end
    const bool narrow = e_hi - b_lo < 2.0 * inside;
    const double mid = 0.5 * (b_lo + e_hi);
    double x0 = 0.0;
    double x1 = 0.0;
    if (is_begin) {
        x0 = b_lo - outside;
        x1 = narrow ? mid : b_lo + inside;
    } else {
        x0 = narrow ? mid : e_hi - inside;
        x1 = e_hi + outside;
    }
    GuiRectF r;
    r.x = x0;
    r.y = row.y;
    r.w = std::max(0.0, x1 - x0);
    r.h = row.h;
    return r;
}

// (THE POPUP LISTS' SCROLL ARROW BUTTON — paint_trim_arrow_button, up and
// down, the trim caps' left and right gone with the view bar 2026-10-09 —
// went with the popup lists' scroll bar, 2026-10-14.)

TrimBarTones trim_bar_tones(const TrimBarLook& look) {
    // THE ONE TABLE (the declaration, render.h), first match; a sweep's
    // moving end is an end held (architect 2026-11-20).
    const GuiPalette& p = palette();
    TrimBarTones t;
    t.body = p.trim_span_face;
    const auto with_caps = [&](TrimBarPart part, GuiColor tone) {
        t.caps      = true;
        t.begin_cap = p.text;
        t.end_cap   = p.text;
        if (part == TrimBarPart::Body)  t.body = tone;
        if (part == TrimBarPart::Begin) t.begin_cap = tone;
        if (part == TrimBarPart::End)   t.end_cap = tone;
    };
    const TrimBarPart held =
        look.held != TrimBarPart::None ? look.held : look.swept;
    if (held != TrimBarPart::None) {
        with_caps(held, p.selected_fill);
    } else if (look.hovered != TrimBarPart::None) {
        with_caps(look.hovered, p.trim_span_hover_face);
    }
    return t;
}

void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect trim_bar,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       long long viewport_lattice_origin,
                       const TrimRange& trim,
                       const std::function<TrimBarLook()>& look,
                       TrimBarHit* out_hit) {
    // COLD FIRST, so every early return below publishes "nothing grabbable"
    // over a lane that painted no bar (the contract at the declaration).
    if (out_hit) *out_hit = TrimBarHit{};
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (trim_bar.w <= 0 || trim_bar.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (waveform_area.w <= 0) return;

    // Both bounds resolve through the ONE shared place owner: .x_raw is the
    // bound's exact place (the slider's box), .x the grabs' (the EOF-wall
    // clamp in it), an offscreen one's past the lane's edge. The bar spans
    // between them wherever they stand, so the places are computed
    // unconditionally.
    const TrimBoundColumn bc = trim_bound_column(
        static_cast<double>(trim.begin), viewport_start_sample,
        viewport_end_sample, viewport_lattice_origin, waveform_area.w);
    const TrimBoundColumn ec = trim_bound_column(
        static_cast<double>(trim.end), viewport_start_sample,
        viewport_end_sample, viewport_lattice_origin, waveform_area.w);

    // THE BAR'S COLUMNS ARE THE WAVEFORM AREA'S: waveform_area's x and its
    // effective width, the panel's whole width (send Z, 2026-11-19: edge to
    // edge; render.h's well_columns), across which paint_trim paints the
    // lane, its trough and its lines.
    const int lane_x   = waveform_area.x;
    const int lane_w   = waveform_area.w;   // the effective width
    const int lane_y   = trim_bar.y;
    const int lane_h   = trim_bar.h;

    // THE TROUGH (render.h's well paragraph; architect 2026-11-17, session
    // 140, ruling 4 and D1): its rows (trim_trough_rect, the one statement)
    // the pill's seat below; its FLAT `trim_bar_face` and the lane's lines
    // are paint_trim's ground step, laid before this pass on every frame
    // class (architect 2026-11-21, send GG).
    const GuiRect trough_r = trim_trough_rect(trim_bar, lane_x, lane_w);
    const int trough_y = trough_r.y;
    const int trough_h = trough_r.h;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_rectangle(cr, lane_x, lane_y, lane_w, lane_h);
    cairo_clip(cr);

    const GuiRect lane{lane_x, lane_y, lane_w, lane_h};

    // THE SPAN, ARC'S SLIDER — A PILL (architect 2026-11-17, session 140,
    // ruling 4: "The scroll bar and the trough should be accurate to whatever
    // Arc uses"). THE SLIDER'S BOX is the trim window from the begin bound's
    // exact place through the end bound's plus one px, UNCUT, ON THE BOUNDS'
    // UNCLAMPED PLACES (TrimBoundColumn::x_raw — the places
    // trim_span_columns cuts for the sweep highlight; the EOF-wall clamp is
    // the grabs' alone, render.h: THE TRIM REFLECTS THE WAVEFORM'S COLUMNS,
    // even where the waveform is wrong, architect 2026-11-03 — the box, the
    // bounds, the grabs and the highlight); THE PILL IS ITS VISIBLE CORE
    // (render.h's THE SLIDER'S BOX AND ITS CORE; architect 2026-11-18:
    // "let's just make it as realistic as we can"): ITS ROWS
    // trim_pill_h_px (6 W) CENTERED ON THE TROUGH, real edges, `seat` of
    // trough above and below (the inset, render.h);
    // ITS ENDS the box's ends drawn in by the same inset, AT THEIR REAL
    // PLACES WHEREVER THEY FALL, on screen or past the strip's edge, round
    // at both, radius half the pill's height. THE SHORT SPAN, the one
    // statement of the rule: the
    // inset at each end is
    //     min(seat, max(0, (S − pill height) / 2)),
    // S the box's width, the end bound's place plus one px less the begin
    // bound's — continuous in S, no pop under a zoom: a box at least the
    // pill's height plus two seats wide keeps the whole seat at each bound;
    // between the pill's height and that, the pill is a disc of the pill's
    // height centered in the box; under the pill's height the pill is the
    // box itself, a pill narrower than two radii taking half its width as the
    // radius, so its round ends meet and never overlap (the rounded rect's
    // own clamp). THE CUT IS THE LANE'S CLIP, set above on the waveform
    // area's whole columns (the strip's edge, so nothing reaches the frame):
    // the stamp takes the context's clip boxes and touches no pixel outside
    // them, each pixel inside at its exact coverage of the whole core
    // (CoverageStampPass::rounded_rect; cairo's road where the target is
    // refused fills the same path under the same clip). So a bound on
    // screen shows its round end the seat inside it, a core running past the
    // edge is cut square and flush there ("it goes on"), and a round end
    // leaving the strip is cut by it column by column — the no-grid rule,
    // render.h's `hull` block, nothing jumping as a bound crosses the edge
    // (architect 2026-11-18): the box reads the unclamped places, so the
    // grab's EOF-wall clamp never holds a core end while its bound moves.
    // An end wholly past the edge is drawn at one pill height beyond it, its
    // round corners still wholly outside, so the stamp's whole-cell arithmetic
    // stays bounded and no pixel changes. Every state below wears the same
    // geometry. The pill stands inside the
    // lane's rows and clip, so the trim lane's whole rows, every trim
    // write's and drag edge's damage (top_trim_row_area), hold it. Painted
    // at each pixel's exact coverage through the coverage stamp
    // (CoverageStampPass, cool_edit_paint.h; coverage_of_rounded_rect,
    // coverage_stamp.h) over the trough paint_trim laid. ITS TONE and whether
    // the caps show are `look`'s through the one table (trim_bar_tones,
    // render.h): at rest `trim_span_face` (Arc's scrollbar
    // slider) and no caps. No outline. NO PLAYHEAD IN THE BAR (architect
    // 2026-10-09 ~14:30, "I thought we agreed no dots on the trim bar").
    //
    // THE CAPS (render.h's THE TRIM BAR'S CAPS AND GRABS; architect
    // 2026-11-20) ARE THE PILL'S OWN ENDS: where the look
    // shows them the pill is stamped ONCE in three regions cut by two
    // vertical lines (CoverageStampPass::rounded_rect_bands) — the end's cap
    // from the right tip `cap_w` in (the pill's radius plus the straight
    // run; the whole pill when the pill is shorter), the begin's cap from
    // the left tip `cap_w` in or to the end cap's line, whichever comes
    // first (the end's over the begin's, "like the flags do"), the body
    // between, empty where the caps meet — so both caps stand at every
    // length, the pill's short forms included. On the tablet the cap is
    // 10 x 12 device px, rows 148 .. 159, its line 16 px inside its bound.
    // EXACT: every pixel is the pill's exact coverage, its tone each
    // region's weighed by the pill's exact area in that region's part of
    // the pixel, laid and rounded once — through a round end as through the
    // straight run, at any place of a line, the two lines in one pixel
    // included; no pixel is laid twice, so no seam shows. The lines read the
    // core's real ends, the box's unclamped places, and the lane's clip cuts
    // the regions as it cuts the pill (the drawn ends' bound below changes
    // no pixel on the strip). THE CAIRO ROAD, where the target is refused,
    // is the stamp's note (cool_edit_paint.h). With no caps the pill is the
    // plain rounded rect in the body's tone.
    const double pill_h = std::min(static_cast<double>(trim_pill_h_px()),
                                   static_cast<double>(trough_h));
    const double seat = (static_cast<double>(trough_h) - pill_h) / 2.0;
    const double box_lo = bc.x_raw;
    const double box_hi = ec.x_raw + 1.0;
    const double inset =
        std::min(seat, std::max(0.0, (box_hi - box_lo - pill_h) / 2.0));
    const double core_lo = box_lo + inset;
    const double core_hi = box_hi - inset;
    const double strip_w = static_cast<double>(lane_w);
    const double cap_w =
        pill_h / 2.0 + static_cast<double>(trim_cap_straight_px());

    // THE GRABS, as painted: the span's two end zones and the body between
    // them (render.h's THE TRIM BAR'S CAPS AND GRABS, trim_endcap_rect,
    // trim_bridge_gap) — each zone cut to the lane's painted width, the clip
    // the pixels take, so a zone overrunning the lane (its 2 W outside reach
    // past a bound at the strip's edge among them) claims its visible
    // stretch alone, and a window wholly off one side publishes no zone on
    // that side and an empty bridge. REAL EDGES (architect 2026-11-15, the
    // no-grid arc): the zones stand on the span's two real ends — the
    // bounds' own places, around the caps drawn off them (architect
    // 2026-11-18 / 2026-11-20) — AS THE GRABS HAVE THEM, `x`, the EOF-wall
    // clamp in it (render.h's TrimBoundColumn): an in-view bound past the
    // last column's left edge stands on it, so at the strip's right edge a
    // zone can stand up to a px left of the slider's box and its cap, whose
    // places are unclamped (`x_raw`), and a begin on the named start a
    // fraction of a px left of the strip's left edge has its bound at 0.
    // PUBLISHED BEFORE THE LOOK IS ASKED (the declaration's `look`): the
    // zones depend on no tone, and a look decided on the picture this pass
    // paints (a lift's pending answer, AppState::TrimBarHover) reads them.
    const GuiRectF begin_r = trim_endcap_rect(true, lane_x, bc, ec, trim_bar);
    const GuiRectF end_r = trim_endcap_rect(false, lane_x, bc, ec, trim_bar);
    const TrimBridgeGap gap = trim_bridge_gap(bc, ec, trim_grab_inside_px());
    const double lane_lo = static_cast<double>(lane_x);
    const double lane_hi = static_cast<double>(lane_x + lane_w);
    const auto lane_cut = [&](GuiRectF r) {
        const double lo = std::max(r.x, lane_lo);
        const double hi = std::min(r.x + r.w, lane_hi);
        r.x = lo;
        r.w = std::max(0.0, hi - lo);
        return r;
    };
    if (out_hit) {
        if (lane_cut(begin_r).w > 0.0)
            out_hit->begin = {true, lane_cut(begin_r)};
        if (lane_cut(end_r).w > 0.0)
            out_hit->end = {true, lane_cut(end_r)};
        out_hit->published = true;
        out_hit->lane      = lane;
        out_hit->bridge_lo = lane_lo + std::max(gap.lo, 0.0);
        out_hit->bridge_hi = lane_lo + std::min(gap.hi,
                                                static_cast<double>(lane_w));
    }

    const TrimBarTones tones = trim_bar_tones(look());
    CoverageStampPass stamp;
    stamp.begin(cr);
    if (core_hi > core_lo && pill_h > 0.0 && core_hi > 0.0 &&
        core_lo < strip_w) {
        const double lx = static_cast<double>(lane_x);
        const double y0 = static_cast<double>(trough_y) + seat;
        const double radius = std::min(pill_h / 2.0, (core_hi - core_lo) / 2.0);
        const double draw_lo = std::max(core_lo, -pill_h);
        const double draw_hi = std::min(core_hi, strip_w + pill_h);
        if (tones.caps) {
            const double end_lo = std::max(core_lo, core_hi - cap_w);
            const double begin_hi = std::min(core_lo + cap_w, end_lo);
            stamp.rounded_rect_bands(lx + draw_lo, y0, lx + draw_hi,
                                     y0 + pill_h, radius, radius,
                                     lx + begin_hi, lx + end_lo,
                                     tones.begin_cap, tones.body,
                                     tones.end_cap);
        } else {
            stamp.rounded_rect(lx + draw_lo, y0, lx + draw_hi, y0 + pill_h,
                               radius, radius, tones.body);
        }
    }
    stamp.end();

    cairo_restore(cr);
}

// THE STRIKE'S BAR OVER ONE STRUCK RUN — the contract and its two readers
// are at the declaration (render.h): the face's strike band over the run's
// baseline, across the run's advance, nothing rounded.
GuiRectF struck_run_bar(const GuiFont& font, double x, double width_px,
                        double baseline) {
    const GuiStrikeBand band = gui_font_strike_band(font);
    return GuiRectF{x, baseline + band.dy, width_px, band.h};
}

namespace {

// THE WAVEFORM'S COLUMNS ARE THE MARKER LANE'S CLIP (architect 2026-09-26):
// every flag box, every flag-editor box and every hit rect either publishes
// is confined to window x in [x0, x0 + w) — the columns the waveform paints,
// the panel's whole width since the canvas went edge to edge (send Z,
// 2026-11-19) — so a flag running past the last column is cut off at it. These two are the one
// spelling of
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
// THE STEM STASH'S GATE IS THE LINE'S OWN (architect 2026-11-15, the no-grid
// arc: "Only things that are basically impossible to move, like the damages
// and the culls, should stay integer" — the column gate of 2026-11-06
// reopened by him): a selected marker publishes its stem entry whenever ANY
// of the stem's real interval — the one-W line centered on the marker's
// exact place `x`, device px from the waveform's first column — lies inside
// the waveform's columns [0, w): the very span the stem painter draws
// (waveform_line_span, render.h, its emptiness the gate), so a stem slides
// in and out at either edge as far as it reaches. The flag iterator admits
// every such marker (its culls are wider than the line, the note there).
// The one spelling for both lane producers (render_flag_boxes_impl,
// render_history_diff_flags, through paint_cues).
bool stem_meets_waveform(double x, int w) {
    return !waveform_line_span(0, w, x).empty();
}

// Shared flag iteration used by render_flags, its phase-reset analogue and
// the `h` view's diff lane. Invokes `emit(i, exact_x)` for EVERY
// visible marker IN STORE ORDER; the cue painter collects them and orders
// them by place itself (the overlap rule paints the labels left to right
// and the triangles left to right, render.h's marker-lane paragraph).
//
// THE PAINT/HIT INVARIANT. `exact_x` — THE MARKER'S EXACT PLACE (architect
// 2026-11-14, send J; the labels and the published rects on it too since
// the no-grid arc, 2026-11-15: displayed_x_at, device px from the
// waveform's first column) — is computed ONCE here: the chip's apex, the
// label's anchor and the stem's center. The painter draws from it, the hit
// rects are published from it, and the stem is published at it, so all
// three are one number by construction. The marker's COLUMN is this
// iterator's own, for its cull alone.
template <typename MarkerVec, typename Emit>
void iterate_visible_flags_impl(
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
    // Map columns at the painters' q, given back by the span over the
    // EFFECTIVE waveform width (recovered_painter_spp,
    // warp_frame_map_view.h), never over the surface's own full width, so a
    // flag stands on the very q the plate's bars and the marker stems are
    // placed at, at every window width.
    const double samples_per_pixel = recovered_painter_spp(
        static_cast<int64_t>(viewport_end_sample - viewport_start_sample),
        waveform_width);
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
    // admitted iff its chip can touch the waveform's columns [0, w) —
    // col - reach < w, the chip's PAINTED REACH left of its column
    // (cue_chip_paint_reach_px, render.h: the chip stands at its mark's
    // exact x, architect 2026-11-14, send J). So a marker just past
    // the last column (the song's last half-column at the right wall, or a
    // marker just past the viewport's end mid-song) is admitted and paints
    // its triangle's left part ALONE on the last columns — every caller clips
    // its paint and its hit rect to [0, w), so nothing paints on the well's
    // frame beside w. Its STEM is
    // published where any of the one-W line reaches the columns
    // (stem_meets_waveform), and source_frame_off_right_edge
    // (warp_frame_map_view.h) still refuses AUTHORING there — a triangle's
    // half is not the marker's column. The column is displayed_column_at's —
    // the refusal's own rounding over the same q — so the cull and the
    // refusal cannot disagree on one basis. The sample compare below is only
    // a PREFILTER (it keeps the int cast bounded), set one column wider than
    // the triangle's reach so it never drops a marker the column test would
    // admit.
    // THE CULLS ARE WIDER THAN EVERYTHING THE GATES ADMIT: a stem's line
    // reaches half a W from its place and its place lies within half a px of
    // the column, so a line that touches the columns has its column within
    // W / 2 + 1 of them — inside the chip's reach on the right (H + 1, H the
    // chip's rows, 4 W) and inside the label's width bound on
    // the left — and no cull here drops a stem the gate would draw.
    const int reach = cue_chip_paint_reach_px();
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
        const double exact_x = displayed_x_at(
            ms, static_cast<double>(viewport_start_sample), samples_per_pixel,
            viewport_lattice_origin);

        emit(static_cast<int>(i), exact_x);
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
// own width, unrounded, or nothing where no cells paint. THE ONE MEASURER. The
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
    double lower_w = 0.0;   // the token's run's own advance, unrounded
    double upper_w = 0.0;
};

static IterCellLayout measure_iter_cells(const GuiFont& font,
                                         const IterCellText& cells) {
    IterCellLayout l;
    if (!cells.present) return l;
    l.present   = true;
    l.lower_run = text_shape::memoized_run(font, cells.lower);
    l.upper_run = text_shape::memoized_run(font, cells.upper);
    l.lower_w   = l.lower_run->width_px;
    l.upper_w   = l.upper_run->width_px;
    return l;
}

// THE LABEL'S SEGMENT BOXES (render.h's marker-lane paragraph and the
// label's horizontal lengths), as offsets from the marker's EXACT PLACE: the
// first present segment's box opens the fill lead past the mark (the
// label's lead less the fill's pad, render.h's cue_fill_lead_px — the lead
// the chip's rows and the gap, so the boxes follow the triangle's rows,
// architect 2026-10-26 ~12:40; the gap from the chip's own corner since
// 2026-11-19, send AA), each box
// is ITS TEXT'S OWN WIDTH, UNROUNDED, with the fill's pad either side (the
// text at its box's start plus the pad — the label's lead), and each next
// box stands the segment gap past the one before — REAL OFFSETS (architect
// 2026-11-15, the no-grid arc: a segment's width is its run's advance and
// the sums are sums of those, so the second and third segments' text stand
// exactly their step past the first's end). `w` the three texts' widths,
// `present` which stand. THE ONE LAYOUT the flag pass, the history lane and
// the editor read.
struct CueSegmentBoxes {
    double x0[3] = {0.0, 0.0, 0.0};
    double x1[3] = {0.0, 0.0, 0.0};
};
static CueSegmentBoxes cue_segment_boxes(const double (&w)[3],
                                         const bool (&present)[3]) {
    const double pad = static_cast<double>(cue_fill_pad_px());
    CueSegmentBoxes b;
    double x = static_cast<double>(cue_fill_lead_px());
    for (int s = 0; s < 3; ++s) {
        b.x0[s] = x;
        b.x1[s] = x;
        if (!present[s]) continue;
        b.x1[s] = x + 2.0 * pad + w[s];
        x = b.x1[s] + static_cast<double>(cue_segment_gap_px());
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
// render.h, owns the look): THE CHIP'S fill (CoverageStampPass::chip, the
// whole triangle, no outline), whether the cue PUBLISHES ITS
// STEM ENTRY (every selected cue, enabled or disabled, MarkerStem: the stem
// is the selection's), and whether its label is STRUCK (show_cue_label's bar). The
// label's other look is the segment's own (inert, or the text tone,
// materials.h's `text`); the selection never reaches it.
struct FlagFace {
    GuiColor fill;
    bool     stem;
    bool     struck;
    // THE CHIP'S HOVER TONE, the far end of its mix under the hovering pen
    // (the table's hover column below): `cue_hover` beside a `cue_fill`
    // fill, `inactive_cue_hover` beside an `inactive_cue` one.
    GuiColor hover_fill;
};

// THE ONE RESOLVER for every cue — both marker columns and the `h` view's
// diff flags (architect 2026-11-05, his pick a2 of session 42 on real captures:
// THE STEM IS THE SELECTION'S, reversing 2026-11-04's coincidence stem —
// "dropped, along with the rest of cool edit stuff except a few details like
// the triangle playhead"; 2026-10-26 ~15:10–15:30, THE STRIKE: "It's very
// odd having anything but red mean invalid. So I think what we should do is
// have coincident markers be shown as a strike through, which is actually
// effective because that's what happens to them. They become one").
// THE TABLE ("stem" the publication of the cue's stem entry, MarkerStem,
// painted wherever it is published; "label" the label's ink, the segment's
// `inert` on the history rows):
//   side      flag        fill          stem      label      strike
//   LIVE      DISABLED    inactive_cue  selected  inert      `strike`
//   LIVE      ENABLED     cue_fill      selected  sidebar    `strike`
//   REMOVED   any         inactive_cue  selected  inert      always
//   ADDED     any         cue_fill      selected  sidebar    never
//   CHANGED   any         cue_fill      selected  sidebar    per segment
// — warp or phase reset alike. THE LIVE ROWS: EVERY ENABLED CHIP IS
// THE `cue_fill`, THE ACCENT (Default's FFAD56, the lit keys' face;
// architect 2026-11-13, session 119: "we revert the rule so that markers
// get accent color instead of icon glyph color"), selected or not, WITH NO
// OUTLINE — the bare triangle of the nominal rows (architect 2026-11-19,
// send AA, RULINGS §8 / §11: "I would consider removing the outline of the
// chips because, for example, the scroll bar has no outline"; "I think I
// prefer the smaller version without the outline"). EVERY
// SELECTED CUE, ENABLED OR DISABLED, ADDS THE STEM (architect
// 2026-11-11, session 106: "disabled markers also get the dark stem, same as
// enabled ones") — a solid one-W `cue_outline` line from the chip's apex
// row down the canvas's whole height at the cue's exact place, in the
// playhead's line's place where the two coincide (paint_marker_stems), the
// playhead on it or not; no
// other stem exists (Live paints none at rest). THE DISABLED AXIS IS THE
// CHIP'S AND THE LABEL'S ALONE, THE SELECTION THE STEM'S ALONE: the two
// never cross, so the stem stands through a disable toggle of a selected
// marker while its chip flips. A DISABLED CHIP IS SOLID GREY (architect
// 2026-11-05, "disabled marker gets dim label"): the `inactive_cue` 5D5D5D
// fill alone, the playhead on it or not, selected or not; THE STEM STAYS `cue_outline` on
// every selected cue, disabled too — the stem is the one mark of the
// selection. THE CHIP CARRIES THE SELECTION, THE PLAYHEAD THE FOCUS (architect
// 2026-11-05: "only 1 playhead onscreen ever"): the focused member has no
// paint of its own — every click and toggle lands the playhead on it
// (land_playhead_on_marker) — and THE RESOLVER NEVER SEES THE PLAYHEAD.
// A LIVE CUE'S STRIKE: `strike` is the warp column's red set
// (warp_red_flag_set_cached — the coincident run, the same frame or under one
// millisecond, disabled rows counted, 2026-11-17; a label ref the render falls
// back from; the definitions of a label two live definitions hold,
// 2026-10-29). A phase reset is never struck (architect 2026-11-17).
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
// rows. An inert label's text and its bar are the inert label (`text_dim`)
// (CueSegment); the bar is part of the text (show_cue_label).
// THE LABELS' TWO INKS (architect 2026-11-19, send Z; DESIGN §4, Arc's
// sidebar): THE SIDEBAR TEXT `sidebar_text` (Arc's $dark_sidebar_fg,
// arc_dark BAC3CF) on every live label, and THE INERT LABEL `text_dim`
// (Arc's insensitive text, arc_dark 7E838F; the inert look's reason
// 2026-10-10 ~12:20, "the arrow needs to be legible for me, but the text
// doesn't … since it's disabled, it's inert") on every INERT label — a
// disabled cue's segments, a tie follower's cells and a removed history
// item's label — selected or not. THE LABEL'S INK AND THE CHIP'S FILL PART
// HERE: the disabled chip keeps `inactive_cue` (Default 5D5D5D, the one
// inert chip tone) while its label wears `text_dim`, the text tones being
// Arc's (until 2026-11-19 the one `inactive_cue` inked both). A DISABLED
// STRUCK CUE — the red set counts disabled rows
// (warp_red_flag_set_cached's (3)) — is the grey chip with its inert label
// struck.
// THE HOVER'S FOUR PAIRS (architect 2026-11-21, send FF: "The markers could
// get a hover action ... if the pen is on the marker's lane, the marker
// flags would light up"; every tone arc_dark's own, Arc's numbers): A CUE
// THE HOVERING PEN IS ON — where a tap there would act on it, the faces'
// gate (AppState::FaceFades' THE MARKER'S FACE, which owns the gate, the
// fade, the key and the damage) — wears, of each tone below, THE MIX OF ITS
// REST AND ITS HOVER BY ITS FACE'S STORED WEIGHT (StampTone, materials.h;
// Arc's 200 ms both ways, a reversal from the value reached):
//   chip   `cue_fill`      (arc_dark 5294E2) -> `cue_hover`          (639FE5,
//          Arc's hover of a blue thing) — every row whose fill is the accent;
//   chip   `inactive_cue`  (666A74)          -> `inactive_cue_hover` (90939B)
//          — every row whose fill is the disabled grey;
//   label  `sidebar_text`  (BAC3CF)          -> `text`               (D3DAE3)
//          — a live label;
//   label  `text_dim`      (7E838F)          -> `inactive_cue_label_hover`
//          (9DA3AD) — an inert label (cue_label_ink below).
// A DISABLED MARKER HOVERS TOO, IN ITS OWN PAIR (architect 2026-11-21: "a
// disabled button means that it can't be clicked, whereas our disabled
// marker means that it's ignored by the parser, but otherwise fully
// clickable and actionable ... the analogy doesn't quite apply"), and a
// hovered disabled marker still reads disabled — ITS LABEL BY BEING DIMMER
// (9DA3AD against an enabled label's BAC3CF at rest), ITS CHIP BY ITS HUE:
// 90939B is a GREY of about the accent's brightness (relative luminance
// 0.292 against 5294E2's 0.285), so it is not dimmer than the enabled chip
// at rest but grey against the accent. THE SELECTION CHANGES NOTHING OF IT
// (the stem stands as it stands); A STRIKE IS LAID IN THE LABEL'S INK AS
// MIXED (show_cue_label: the bar is part of the text); the `h` view's items
// take the pair of their row's fill and their segments' inertness, a
// removed item the disabled pair, an added or changed one the enabled pair.
FlagFace resolve_flag_face(GuiFlagSide side, bool disabled, bool strike,
                           bool selected) {
    const GuiPalette& p = palette();
    const FlagFace enabled_face{p.cue_fill, selected, false, p.cue_hover};
    const FlagFace disabled_face{p.inactive_cue, selected, false,
                                 p.inactive_cue_hover};
    const auto with_strike = [](FlagFace f, bool struck) {
        f.struck = struck;
        return f;
    };
    switch (side) {
        case GuiFlagSide::Live:
            return with_strike(disabled ? disabled_face : enabled_face,
                               strike);
        case GuiFlagSide::Removed:
            return with_strike(disabled_face, true);
        case GuiFlagSide::Added:
            return with_strike(enabled_face, false);
        case GuiFlagSide::Changed:
            return with_strike(enabled_face, false);
    }
    return with_strike(enabled_face, strike);
}

// A LABEL'S INK AS PAINTED (the table above): an inert label's `text_dim`
// toward `inactive_cue_label_hover`, every other's `sidebar_text` toward
// `text`, by the cue's hover weight — the one color at 0, byte for byte the
// resting road.
static StampTone cue_label_ink(bool inert, double hover) {
    const GuiPalette& p = palette();
    return inert ? StampTone(p.text_dim, p.inactive_cue_label_hover, hover)
                 : StampTone(p.sidebar_text, p.text, hover);
}

// ONE SEGMENT OF A CUE'S LABEL, as the pass paints it: its shaped run (the
// memo's shared handle, CueRun — null exactly when the segment is absent),
// its face box [x0, x1) in window x, REAL EDGES (the text at x0 plus the
// fill's pad; cue_segment_boxes off the cue's exact place),
// and its look — the inert label or the sidebar text, the selection never
// reaching it (2026-11-02) — and whether it is STRUCK.
struct CueSegment {
    bool   present  = false;
    CueRun run;
    double x0       = 0.0;
    double x1       = 0.0;
    // INERT: a disabled marker's segment or a tie follower's cell, the
    // inert label (`text_dim` since 2026-11-19, send Z).
    bool   inert    = false;
    // STRUCK (architect 2026-10-26): its side's `struck` (resolve_flag_face)
    // — every segment of a coincident live marker; in the `h` view a removed
    // item's one segment and a changed item's older one (2026-11-11,
    // session 107).
    bool   struck   = false;
};
// THE CUE LABEL'S ONE TEXT PAINTER: the run in `ink` — a tone, the label's
// rest and hover inks mixed by its cue's hover weight as painted
// (cue_label_ink; resolve_flag_face's THE HOVER'S FOUR PAIRS), plain at
// rest — NO SHADOW (architect
// 2026-11-02, the well's part of the flat look, "no shadow anywhere"; its
// shadow one label quantum right and down, 2026-10-25 to 2026-11-02, is git
// history). Every label look on the lane's face goes through here —
// resting, struck, inert, and the flag editor's riding cells; the
// editor's FIELD text does not, and never strikes (the text there is being
// edited).
// THE STRIKE IS PART OF THE TEXT (architect 2026-10-26 ~15:10: "just the
// strike through on the text"): where `struck`, the run's strike bar
// (struck_run_bar, render.h — the one geometry, the `h` view's cue tooltip
// its other reader) in the text's ink. One rect a segment; nothing
// memoized.
// THE TEXT STANDS WITH ITS MARK, EXACTLY (architect 2026-11-15, the no-grid
// arc: "looks better because it just feels more cohesive now. The whole
// thing is just one unit" — the glide's trial of 2026-11-14, send J, is the
// rule): `x` IS THE LABEL'S EXACT PEN — the cue's exact place plus the
// label's lead and the segments before it, a real x, one pen handed to the
// shaper's placement (text_shape's place_glyphs unchanged; the raster road
// places each glyph at its own exact x, text_raster.h) — so the label moves
// with its chip and its stem by the same real amount under every pan and
// zoom; and THE BAR IS THE RUN'S OWN at that pen (struck_run_bar, an exact
// rect) through a coverage stamp (CoverageStampPass, cool_edit_paint.h).
// WHERE THE LABEL IS CUT — `visible`, the intervals of window x no later
// label's box covers (paint_cues), null for a label nothing covers (the
// editor's riding cells) — THE TEXT IS CUT ON ITS OWN MASK at exact
// coverage (text_shape::show_shaped_run_within: no cairo clip is set, so the
// stamp passes of this rebuild keep the direct road) and THE BAR IS THE
// BAR'S INTERSECTION WITH EACH INTERVAL, a rect with real edges; where
// nothing cuts it the bar is the whole rect, and on whole edges it writes
// the ink's word, the bytes an opaque fill of its cells leaves.
static void show_cue_label(cairo_t* cr, const text_shape::ShapedRun& run,
                           double x, double baseline, const StampTone& ink,
                           bool struck, const text_raster::XSpan* visible,
                           int nvisible) {
    set_stamp_tone_source(cr, ink);
    if (visible != nullptr)
        text_shape::show_shaped_run_within(cr, run, x, baseline, visible,
                                           nvisible);
    else
        text_shape::show_shaped_run(cr, run, x, baseline);
    if (struck) {
        const GuiRectF bar =
            struck_run_bar(run.font, x, run.width_px, baseline);
        if (!(bar.w > 0.0) || !(bar.h > 0.0)) return;
        CoverageStampPass stamp;
        stamp.begin(cr);
        if (visible == nullptr) {
            stamp.rect(bar.x, bar.y, bar.x + bar.w, bar.y + bar.h, ink);
        } else {
            for (int i = 0; i < nvisible; ++i)
                stamp.rect(std::max(bar.x, visible[i].x0), bar.y,
                           std::min(bar.x + bar.w, visible[i].x1),
                           bar.y + bar.h, ink);
        }
        stamp.end();
    }
}

// ONE CUE, laid out: its marker (or diff flag) index, its frame's EXACT
// PLACE `x` in device px from the waveform's first column (architect
// 2026-11-14, send J; the iterator's exact_x — the chip's apex, the label's
// anchor and the published stem's place), its resolved face (the chip's
// colors and whether it publishes a stem entry), and its label's
// up-to-three segments in their left-to-right order.
struct CueDraw {
    int                       index = -1;
    double                    x     = 0.0;
    FlagFace                  face{};
    std::array<CueSegment, 3> seg{};
    // ITS IDENTITY ACROSS REBUILDS (CueKey, render.h) and THE HOVER WEIGHT
    // IT IS PAINTED AT — its face's Hover weight as the producer read it
    // (CueLaneHover), or as cue_pass_sync_hover repainted it since: always
    // what the flag cache's surface shows of this cue.
    CueKey                    key{};
    double                    hover = 0.0;
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
// AND THE PASS BEFORE IT (architect 2026-11-16, the beats): `previous` is
// the layout of the pass that ran before the last one, as that pass left it
// — its cues in its painted order — swapped out of `cues` by the flag
// cache's rebuild ahead of its pass (cue_pass_keep_previous), so the rebuild
// can damage the cues that differ and no others (cue_pass_changed_rects,
// below the producers; the rule is there). `order` and `kept` are that
// comparison's two working vectors.
// AND THE PASS'S PLACE (architect 2026-11-21, send FF: the lanes' hovers):
// the lane, the columns' origin and their width the last paint_cues laid
// `cues` out on, so cue_pass_sync_hover repaints a cue on the very geometry
// the surface was painted at; `regions` its working vector. AND `hovered`,
// THE COUNT OF `cues` PAINTED AT A HOVER WEIGHT OTHER THAN 0 (architect
// 2026-11-21, send FF: the lanes' at-rest exit,
// GuiPaintHandler::lane_cues_rest): counted by paint_cues over the layout
// it paints, 0 at cue_pass_keep_previous (`cues` emptied), kept by
// cue_pass_sync_hover at each weight it writes — always the number of cues
// the surface shows lit, read without a walk.
struct CuePassScratch {
    std::vector<CueDraw>             cues;
    std::vector<double>              lo;
    std::vector<double>              hi;
    std::vector<text_raster::XSpan>  spans;
    std::vector<CueDraw>             previous;
    std::vector<std::pair<int, int>> order;
    std::vector<unsigned char>       kept;
    GuiRect                          lane{0, 0, 0, 0};
    int                              x0 = 0;
    int                              w  = 0;
    std::vector<std::pair<int, int>> regions;
    int                              hovered = 0;
};
static CuePassScratch g_cue_pass_scratch;

// THE CUES' ONE PAINTER (render.h's marker-lane paragraph: the overlap rule,
// the states; the lengths program_spec.h's), shared by both marker columns
// and the `h` view's lane: the labels LEFT TO RIGHT, each owning an opaque
// box from its own chip's left edge to its text's end, so a later cue
// hides an earlier label only across its own extent, a selected label as at
// rest (2026-11-02); then
// the chips LEFT TO RIGHT, each in its face's fill, no outline
// (resolve_flag_face; architect 2026-11-19, send AA) — chip and label each
// the mix of its rest and hover tones by the cue's hover weight (the table's
// hover pairs; CueDraw::hover), plain at rest; then the
// publication — one FlagHitRect per cue (its label box and its
// chip, both clipped to the waveform's columns),
// and one MarkerStem per cue whose face says `stem` — every SELECTED cue,
// enabled or disabled, an `h` item one cue — whose stem's line reaches the
// waveform's columns (stem_meets_waveform; paint_marker_stems paints every
// entry, architect 2026-11-05).
// EVERYTHING STANDS ON THE CUE'S EXACT PLACE (architect 2026-11-15, the
// no-grid arc): the label's pen, each segment's box, the occluding box's two
// edges, the chip and every published rect are the cue's exact x plus their
// own real offsets — one number each, painted and published alike.
// `lane` is the
// marker lane, `x0` / `w` the waveform's columns the pass is clipped to (the
// caller set the clip). Cues arrive in store order and are ordered here by
// place (stable).
// A label segment's TEXT ORIGIN, its EXACT pen x: its box start plus the
// fill pad. One expression for the label pass and the publication
// (FlagHitRect::text_x), so the cue tooltip's text stands on the label's
// own pen.
static double cue_segment_text_x(const CueSegment& s, int pad) {
    return s.x0 + static_cast<double>(pad);
}

// EACH LABEL'S OCCLUDING BOX [lo, hi) in window x, REAL EDGES: from its own
// chip's left edge (its exact place less the chip's hit extent, `half`) to
// its last segment's end; empty where no segment paints. The text stands
// where its segments put it; the box only reaches back to the chip. Into
// the pass's kept scratch (CuePassScratch), refilled here — the one layout
// of the boxes paint_cues and cue_pass_sync_hover both cut on.
static void lay_cue_boxes(const std::vector<CueDraw>& cues, int x0,
                          double half, std::vector<double>& lo,
                          std::vector<double>& hi) {
    const int n = static_cast<int>(cues.size());
    lo.assign(static_cast<std::size_t>(n), 0.0);
    hi.assign(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        const CueDraw& c = cues[static_cast<std::size_t>(i)];
        double b = 0.0;
        bool any = false;
        for (const CueSegment& s : c.seg) {
            if (!s.present) continue;
            b = s.x1;
            any = true;
        }
        const double left = static_cast<double>(x0) + c.x - half;
        lo[static_cast<std::size_t>(i)] = left;
        hi[static_cast<std::size_t>(i)] = any ? std::max(left, b) : left;
    }
}

// LABEL i's VISIBLE INTERVALS: its box [lo, hi) less every later non-empty
// box, found in one walk over the later boxes that start inside it (the
// cues are ordered by place, so a later box's left edge never lies left of
// this one's) — into `spans`, empty where nothing of it shows.
static void cue_label_spans(int i, const std::vector<double>& lo,
                            const std::vector<double>& hi,
                            std::vector<text_raster::XSpan>& spans) {
    spans.clear();
    const int n = static_cast<int>(lo.size());
    const double a = lo[static_cast<std::size_t>(i)];
    const double b = hi[static_cast<std::size_t>(i)];
    if (b <= a) return;
    double cursor = a;
    for (int j = i + 1; j < n && cursor < b; ++j) {
        const double ja = lo[static_cast<std::size_t>(j)];
        const double jb = hi[static_cast<std::size_t>(j)];
        if (ja >= b) break;
        if (jb <= ja) continue;
        if (ja > cursor) spans.push_back(text_raster::XSpan{cursor, ja});
        cursor = std::max(cursor, jb);
    }
    if (cursor < b) spans.push_back(text_raster::XSpan{cursor, b});
}

// THE CUES' INK — THE LABELS, THEN THE CHIPS, EACH LEFT TO RIGHT — of every
// cue `paints(i)` admits, under the clip the caller set (paint_cues: the
// lane's rows over the pass's columns; cue_pass_sync_hover: a region of
// them), on the boxes `lo` / `hi` lay_cue_boxes laid. The rules are
// paint_cues' (below); this is their one body, so a cue repainted for its
// hover is painted by the very code that painted it.
template <class Paints>
static void paint_cue_ink(cairo_t* cr, const GuiRect& lane, int x0,
                          const std::vector<CueDraw>& cues,
                          const std::vector<double>& lo,
                          const std::vector<double>& hi, Paints&& paints) {
    const int pad      = cue_fill_pad_px();   // the label's fill pad
    // The chip's rows (render.h's cue_chip_h_px, the whole hull of its
    // height): the lane's last rows, its apex on THE BOTTOM EDGE OF THE
    // LANE'S LAST ROW, `chip_yb`.
    const int chip_top = lane.y + cue_above_chip_h_px();
    const int chip_h   = cue_chip_h_px();
    const double chip_yb = static_cast<double>(chip_top + chip_h);
    const double base  = static_cast<double>(lane.y) + cue_baseline_px();
    const int n = static_cast<int>(cues.size());
    std::vector<text_raster::XSpan>& spans = g_cue_pass_scratch.spans;
    for (int i = 0; i < n; ++i) {
        if (!paints(i)) continue;
        const CueDraw& c = cues[static_cast<std::size_t>(i)];
        cue_label_spans(i, lo, hi, spans);
        if (spans.empty()) continue;
        for (const CueSegment& s : c.seg) {
            if (!s.present) continue;
            // The label's ink (resolve_flag_face's table and its hover's
            // pairs): the inert label `text_dim` or the sidebar text
            // `sidebar_text`, selected or not, each mixed toward its hover
            // tone by the cue's weight (cue_label_ink); a struck segment's
            // bar in the same ink (show_cue_label). The pen is the
            // segment's exact one.
            show_cue_label(cr, *s.run, cue_segment_text_x(s, pad), base,
                           cue_label_ink(s.inert, c.hover), s.struck,
                           spans.data(), static_cast<int>(spans.size()));
        }
    }
    CoverageStampPass chips;
    chips.begin(cr);
    for (int i = 0; i < n; ++i) {
        if (!paints(i)) continue;
        const CueDraw& c = cues[static_cast<std::size_t>(i)];
        // Its fill mixed toward its hover tone by its weight (the table's
        // hover pairs): plain at rest.
        chips.chip(static_cast<double>(x0) + c.x, chip_yb,
                   StampTone(c.face.fill, c.face.hover_fill, c.hover));
    }
    chips.end();
}

static void paint_cues(cairo_t* cr, const GuiRect& lane, int x0, int w,
                       std::vector<CueDraw>& cues,
                       std::vector<FlagHitRect>* out_hit_rects,
                       std::vector<MarkerStem>* out_stems) {
    std::stable_sort(cues.begin(), cues.end(),
                     [](const CueDraw& a, const CueDraw& b) {
                         return a.x < b.x;
                     });
    const int pad      = cue_fill_pad_px();   // the label's fill pad
    // The chip's HIT extent left of its mark (render.h's
    // cue_chip_hit_left_px): the published chip box's and the label's
    // occluding box's left edge, off the cue's exact place.
    const double half  = static_cast<double>(cue_chip_hit_left_px());
    const int fill_h   = cue_fill_h_px();
    const int chip_top = lane.y + cue_above_chip_h_px();
    const int chip_h   = cue_chip_h_px();
    const int n = static_cast<int>(cues.size());
    // THE PASS'S PLACE, kept beside the layout (CuePassScratch), and the
    // count of its cues painted lit (`hovered`).
    g_cue_pass_scratch.lane = lane;
    g_cue_pass_scratch.x0   = x0;
    g_cue_pass_scratch.w    = w;
    g_cue_pass_scratch.hovered = static_cast<int>(
        std::count_if(cues.begin(), cues.end(),
                      [](const CueDraw& c) { return c.hover != 0.0; }));
    // Each label's OCCLUDING BOX [lo, hi) (lay_cue_boxes), into the pass's
    // kept scratch.
    std::vector<double>& lo = g_cue_pass_scratch.lo;
    std::vector<double>& hi = g_cue_pass_scratch.hi;
    lay_cue_boxes(cues, x0, half, lo, hi);

    // THE LABELS, LEFT TO RIGHT: a later cue's box hides an earlier cue's
    // text across its own extent alone.
    //
    // THE BOX IS NEVER PAINTED AND THE TEXT IS CUT TO WHERE NO LATER BOX
    // COVERS IT (architect 2026-10-12 for the text, the marker slowdown at
    // coarse zoom; 2026-10-13 for the box, the flag cache's solid fills
    // measuring 12.8 % of the CPU; exact since the no-grid arc, 2026-11-15):
    // the box is the lane's own flat face (Live's 2A2A2A since 2026-11-03,
    // materials.h's `lane_face`) — the one color of every pixel under the
    // cues, which the lane's painter lays before this cache is blitted over
    // it — so all a box ever did was hide an earlier label's ink, and a
    // label whose ink is laid only where it shows needs no box at all. The
    // VISIBLE INTERVALS of [a, b) less every later non-empty box
    // (cue_label_spans) are the whole of what this label shows: its text and
    // its strike are CUT TO THEM AT EXACT COVERAGE ON THEIR OWN INK
    // (show_cue_label: the text's mask multiplied by each column's visible
    // length, the bar intersected) — the picture an opaque box laid at its
    // real edges over the earlier ink leaves, column for column, with NO
    // CAIRO CLIP: nothing fractional and nothing multi-rect stands under the
    // stamp passes of this rebuild (the chips', the struck bars'), which keep
    // the direct road (direct_pixel_target: whole clip boxes, a whole
    // translation). A long label before a short one keeps its interval past
    // the short one's end; a label with no interval paints nothing.
    //
    // ONE CLIP FOR THE WHOLE PASS, THE LANE'S ROWS over the waveform's
    // columns (the caller's columns, so the clip only gains its rows) — a
    // whole rectangle, the stamp's clip box: the labels' text and strikes
    // and the chips all stand under it, so no cue pixel leaves the lane's
    // rows however far a glyph overshoots (FlagCache's head,
    // paint_handler.h). Released after the chips.
    //
    // THE CHIPS, LEFT TO RIGHT, over every label: ONE PASS on one
    // prepared target (CoverageStampPass, cool_edit_paint.h), UNDER THE
    // PASS'S CLIP OF THE LANE'S ROWS (whole pixels, the stamp's clip
    // box): EACH CHIP AT ITS MARK'S EXACT x (architect 2026-11-14, send J:
    // the window x of the cue's exact place, CueDraw::x — its apex on the
    // stem's center), on the lane's last rows, its one triangle in its fill,
    // each pixel at its exact coverage over what the labels and the
    // chips to its left laid there, so a right chip cuts its left
    // neighbour's as before, its edge soft. Its contract
    // holds here: the label pass changed no clip (each struck bar's
    // own stamp pass began and ended inside its label), the clip
    // was set before begin() and is released after end(), and the pass's
    // cairo road keeps its own, so the clip, the matrix, the operator and
    // the group are constant throughout the pass.
    // THE INK OF BOTH is paint_cue_ink's one body, every cue admitted.
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(x0), static_cast<double>(lane.y),
                    static_cast<double>(w > 0 ? w : 0),
                    static_cast<double>(lane.h));
    cairo_clip(cr);
    paint_cue_ink(cr, lane, x0, cues, lo, hi, [](int) { return true; });
    cairo_restore(cr);

    // THE PUBLICATION, left to right — ON THE CUE'S EXACT PLACE (architect
    // 2026-11-15, the no-grid arc: a hit box stands on its mark's real
    // edges; FlagHitRect's fields are real x): the very edges the label pass
    // cut on and the chip's hit extent off the same exact x.
    for (int i = 0; i < n; ++i) {
        const CueDraw& c = cues[static_cast<std::size_t>(i)];
        const double a = lo[static_cast<std::size_t>(i)];
        const double b = hi[static_cast<std::size_t>(i)];
        if (out_hit_rects) {
            FlagHitRect r;
            r.marker_index = c.index;
            r.key          = c.key;
            // The label's box; each boundary its segment's box
            // start, collapsing onto the box's right edge where the segment
            // did not paint.
            FlagHitRect label;
            label.x = a;
            label.y = static_cast<double>(lane.y);
            label.w = std::max(0.0, b - a);
            label.h = static_cast<double>(fill_h);
            const bool label_on =
                b > a && clip_hit_rect_to_waveform_columns(label, x0, w);
            r.x = label_on ? label.x : 0.0;
            r.y = label_on ? label.y : 0.0;
            r.w = label_on ? label.w : 0.0;
            r.h = label_on ? label.h : 0.0;
            const double right = b;
            const bool cells = c.segments_are_cells;
            r.iter_lower_boundary_x = cells && c.seg[1].present
                                          ? c.seg[1].x0
                                          : right;
            r.iter_upper_boundary_x = cells && c.seg[2].present
                                          ? c.seg[2].x0
                                          : right;
            // The label's text origin: the first painted segment's exact
            // pen x, the very pen the label pass above hands show_cue_label
            // (FlagHitRect::text_x, the cue tooltip's seat).
            if (label_on) {
                for (const CueSegment& s : c.seg) {
                    if (!s.present) continue;
                    r.has_text_x = true;
                    r.text_x     = cue_segment_text_x(s, pad);
                    break;
                }
            }
            // The chip's box: its HIT extent round its exact place
            // (cue_chip_hit_left_px, cue_chip_hit_right_px) over its rows.
            FlagHitRect tri;
            tri.x = static_cast<double>(x0) + c.x - half;
            tri.y = static_cast<double>(chip_top);
            tri.w = half + static_cast<double>(cue_chip_hit_right_px());
            tri.h = static_cast<double>(chip_h);
            if (clip_hit_rect_to_waveform_columns(tri, x0, w)) {
                r.tri_x = tri.x;
                r.tri_y = tri.y;
                r.tri_w = tri.w;
                r.tri_h = tri.h;
            }
            if (label_on || r.tri_w > 0.0) out_hit_rects->push_back(r);
        }
        // The stem's entry, where its line reaches the waveform's columns
        // (stem_meets_waveform): its exact place (MarkerStem, render.h).
        if (out_stems && c.face.stem && stem_meets_waveform(c.x, w))
            out_stems->push_back(MarkerStem{c.index, c.x});
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

// A CUE'S RANK AMONG ITS LIST'S ITEMS ON ITS OWN FRAME (CueKey's ordinal):
// the items before it, in list order, that stand on the same authored
// frame — 0 for every item but a stack's later members. EVERY LIST IT IS
// ASKED OF IS SORTED ASCENDING BY time_frame: the two live columns' stores
// (GuiMarkerStore, marker_store.h: insert_marker places by lower_bound and
// every time-mutating gesture reorders before the store is next read) and
// the `h` view's diff list (GuiPaintHandler::rebuild_history_diff_flags'
// closing stable_sort, waveform_cache.cpp). So the items before i on i's
// frame are exactly the run immediately before it, and the walk back from
// i − 1 while the frame holds counts the very items a walk over all of
// 0 .. i − 1 counts — the same rank, at the cost of the stack's depth, not
// of the list's length (a rebuild runs on every pan or zoom frame).
template <typename Vec>
static int cue_rank_on_frame(const Vec& items, int i) {
    const int64_t f = items[static_cast<std::size_t>(i)].time_frame;
    int rank = 0;
    for (int j = i - 1;
         j >= 0 && items[static_cast<std::size_t>(j)].time_frame == f; --j)
        ++rank;
    return rank;
}

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
    bool iteration_on,
    // THE LANE'S HOVER (CueLaneHover, render.h): the listing every cue's key
    // is taken in, and each cue's hover weight as its face stores it.
    const CueLaneHover& hover) {
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
    // THE LABEL FACE for the whole pass (gui_font.h: Regular Label 12 W, 24
    // device px on the tablet, send Z),
    // every run carrying it (text_shape.h).
    const GuiFont font = gui_font(GuiFace::Label);
    // The pass's kept scratch (CuePassScratch), emptied for this pass.
    std::vector<CueDraw>& cues = g_cue_pass_scratch.cues;
    cues.clear();

    iterate_visible_flags_impl(waveform_width, markers,
                               viewport_start_sample, viewport_end_sample,
                               viewport_lattice_origin,
                               warp_frame_map, drag_overlay,
                               // `iteration_on` widens the bound by the two
                               // cells; the reasoning is at the bound.
                               marker_flag_max_width_px(iteration_on),
        [&](int i, double exact_x) {
            CueDraw c;
            c.index = i;
            c.x     = exact_x;
            // ITS KEY AND ITS HOVER WEIGHT (CueKey, CueLaneHover): its
            // authored frame and its rank among the store's markers on that
            // frame, in the pass's listing.
            c.key   = CueKey{hover.listing,
                             markers[static_cast<std::size_t>(i)].time_frame,
                             cue_rank_on_frame(markers, i)};
            c.hover = hover.weight_of ? hover.weight_of(c.key) : 0.0;
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
            const double widths[3] = {payload->width_px, cl.lower_w,
                                      cl.upper_w};
            const CueSegmentBoxes boxes = cue_segment_boxes(widths, present);
            // The cue's exact place in window x, the segments' anchor.
            const double mark_x =
                static_cast<double>(lanes.columns_x) + c.x;
            for (int s = 0; s < 3; ++s) {
                CueSegment& seg = c.seg[static_cast<std::size_t>(s)];
                if (!present[s]) continue;
                seg.present  = true;
                seg.run      = s == 0 ? payload
                             : s == 1 ? cl.lower_run : cl.upper_run;
                seg.x0       = mark_x + boxes.x0[s];
                seg.x1       = mark_x + boxes.x1[s];
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
                  SuppressedBox suppressed,
                  const CueLaneHover& hover) {
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
        suppressed, iteration_on, hover);
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
                            bool iteration_on,
                            std::vector<FlagHitRect>* out_hit_rects,
                            std::vector<MarkerStem>* out_stems,
                            const std::vector<WarpFrameMapSegment>* warp_frame_map,
                            const DragOverlay* drag_overlay,
                            SuppressedBox suppressed,
                            const CueLaneHover& hover) {
    // NO RESET IS STRUCK (architect 2026-11-17: "phase resets should get no
    // strike b/c they effectively collapse to 1 in the engine already"): the
    // shared pass's strike set is empty on this column.
    static const std::set<int> kNoStrike;
    render_flag_boxes_impl(
        cr, top_strip_area, lanes, waveform_width,
        phase_resets,
        viewport_start_sample, viewport_end_sample,
        viewport_lattice_origin, sample_rate,
        selected_set, kNoStrike,
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
        iteration_on, hover);
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
        const std::vector<WarpFrameMapSegment>* warp_frame_map,
        const CueLaneHover& hover) {
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
    // The live lane cuts a warp cue's TEMPO SCALE, not free text
    // (flag_display_text: the scale capped to `*N.NN` and, where a scale
    // stands, the label definition after it, the `...` standing for whatever
    // was cut — a scale spells min-4 and can run long, and an uncapped one
    // would swamp its neighbors; a cue with no scale paints its whole
    // payload, label definition included); a diff flag's label is the
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
        waveform_width, flags,
        viewport_start_sample, viewport_end_sample,
        viewport_lattice_origin,
        warp_frame_map,
        // NO DRAG OVERLAY: the mode consumes every authoring gesture, so no
        // marker drag can be in flight while this pass runs — and a diff flag is
        // not a marker in any store, so nothing could index it anyway.
        /*drag_overlay=*/nullptr,
        cull_width_px,
        [&](int i, double exact_x) {
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
            c.x     = exact_x;
            // ITS KEY AND ITS HOVER WEIGHT (CueKey, CueLaneHover): the
            // item's frame and its rank among the items on it, in the
            // listing of the commit shown.
            c.key   = CueKey{hover.listing, f.time_frame,
                             cue_rank_on_frame(flags, i)};
            c.hover = hover.weight_of ? hover.weight_of(c.key) : 0.0;
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
            bool   present[3] = {false, false, false};
            double widths[3]  = {0.0, 0.0, 0.0};
            for (int s = 0; s < count; ++s) {
                runs[s]    = text_shape::memoized_run(font, *texts[s].text);
                present[s] = true;
                widths[s]  = runs[s]->width_px;
            }
            const CueSegmentBoxes boxes = cue_segment_boxes(widths, present);
            // The item's exact place in window x, the segments' anchor.
            const double mark_x =
                static_cast<double>(lanes.columns_x) + c.x;
            for (int s = 0; s < count; ++s) {
                CueSegment& seg = c.seg[static_cast<std::size_t>(s)];
                seg.present = true;
                seg.run     = runs[s];
                seg.x0      = mark_x + boxes.x0[s];
                seg.x1      = mark_x + boxes.x1[s];
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

// THE CUE PASS'S OWN CHANGE (architect 2026-11-16, the beats; the contract
// at the declarations, render.h).
namespace {

// Whether two laid-out cues PAINT ALIKE: every input paint_cues reads of a
// cue but its place in the order — its exact place, the chip's two colors
// and its hover weight (the chip's and the label's mix, send FF),
// and each segment's presence, run, box, look and strike; `segments_are_cells`
// beside them, which no pixel reads but the published hit rect does
// (FlagHitRect's two boundaries), so that "no cue differs" also says "no
// published rect differs". THE RUN IS COMPARED BY HANDLE: a run is one
// memoized shaping (CueRun), and the kept previous layout holds its handles,
// so one handle is one text on one face and a re-shaped equal text only
// reads as a difference. NOT `face.stem`: it paints nothing here — the stem
// is the stem painters', off the published entries, which the rebuild
// compares on their own (maybe_rebuild_flag_cache's tail).
bool cue_paints_alike(const CueDraw& a, const CueDraw& b) {
    const auto same_color = [](const GuiColor& p, const GuiColor& q) {
        return p.r == q.r && p.g == q.g && p.b == q.b;
    };
    if (a.x != b.x || a.segments_are_cells != b.segments_are_cells ||
        !same_color(a.face.fill, b.face.fill) ||
        !same_color(a.face.hover_fill, b.face.hover_fill) ||
        a.hover != b.hover)
        return false;
    for (std::size_t s = 0; s < a.seg.size(); ++s) {
        const CueSegment& p = a.seg[s];
        const CueSegment& q = b.seg[s];
        if (p.present != q.present) return false;
        if (!p.present) continue;
        if (p.run != q.run || p.x0 != q.x0 || p.x1 != q.x1 ||
            p.inert != q.inert || p.struck != q.struck)
            return false;
    }
    return true;
}

// Widens the real interval [lo, hi] of window x to hold EVERYTHING ONE CUE
// CAN PAINT OR HIDE (paint_cues): its chip, the real interval [X − H, X + H]
// round its exact place X, taken at the chip's painted reach
// (cue_chip_paint_reach_px, a whole px past the chip's hull); and its
// label's occluding box, from the chip's hit edge left of X — inside the
// reach — through its last segment's end, the interval its own text and
// strike are cut to and across which it hides every earlier label.
void widen_to_cue(const CueDraw& c, int x0, double& lo, double& hi) {
    const double X     = static_cast<double>(x0) + c.x;
    const double reach = static_cast<double>(cue_chip_paint_reach_px());
    lo = std::min(lo, X - reach);
    hi = std::max(hi, X + reach);
    for (const CueSegment& s : c.seg)
        if (s.present) hi = std::max(hi, s.x1);
}

}  // namespace

void cue_pass_keep_previous() {
    std::swap(g_cue_pass_scratch.cues, g_cue_pass_scratch.previous);
    g_cue_pass_scratch.cues.clear();
    g_cue_pass_scratch.hovered = 0;
}

void cue_pass_changed_rects(FlagLaneRects lanes, int waveform_width,
                            std::vector<GuiRect>& out) {
    const GuiRect lane = lanes.marker_lane;
    const int     x0   = lanes.columns_x;
    if (lane.h <= 0 || waveform_width <= 0) return;
    const std::vector<CueDraw>& now = g_cue_pass_scratch.cues;
    const std::vector<CueDraw>& was = g_cue_pass_scratch.previous;
    // The previous cues by marker index (one cue a marker, the iterator's
    // walk), each with its position in the previous painted order.
    std::vector<std::pair<int, int>>& order = g_cue_pass_scratch.order;
    std::vector<unsigned char>&       kept  = g_cue_pass_scratch.kept;
    order.clear();
    kept.assign(was.size(), 0);
    for (std::size_t p = 0; p < was.size(); ++p)
        order.emplace_back(was[p].index, static_cast<int>(p));
    std::sort(order.begin(), order.end());

    // One rect for a cue's previous and present extents together (either
    // may be absent): the hull of the real interval, cut to the waveform's
    // columns as the pass's clip cuts the ink, over the lane's rows — the
    // rows paint_cues' one clip holds every cue pixel to.
    const auto declare = [&](const CueDraw* before, const CueDraw* after) {
        double lo = std::numeric_limits<double>::infinity();
        double hi = -std::numeric_limits<double>::infinity();
        if (before) widen_to_cue(*before, x0, lo, hi);
        if (after)  widen_to_cue(*after, x0, lo, hi);
        const int c0 = std::max(x0, static_cast<int>(std::floor(lo)));
        const int c1 = std::min(x0 + waveform_width,
                                static_cast<int>(std::ceil(hi)));
        if (c1 > c0) out.push_back(GuiRect{c0, lane.y, c1 - c0, lane.h});
    };

    // The present cues in painted order, each against the previous cue of
    // its marker index. THE ORDER: two cues that paint alike in both passes
    // hold their places and their indexes, and the painted order is by place
    // and then by index (paint_cues' stable sort over the store's order), so
    // they hold their order too; a pair whose order flipped is declared, both
    // cues, whatever the comparison said of each (the flip means one of them
    // moved, and what it changes lies in that one's two extents).
    int last_i = -1;
    int last_p = -1;
    for (std::size_t i = 0; i < now.size(); ++i) {
        const auto it = std::lower_bound(order.begin(), order.end(),
                                         std::make_pair(now[i].index, 0));
        if (it == order.end() || it->first != now[i].index) {
            declare(nullptr, &now[i]);   // a cue that entered
            continue;
        }
        const int p = it->second;
        kept[static_cast<std::size_t>(p)] = 1;
        bool differs = !cue_paints_alike(was[static_cast<std::size_t>(p)],
                                         now[i]);
        if (last_i >= 0 && p < last_p) {
            differs = true;
            declare(&was[static_cast<std::size_t>(last_p)],
                    &now[static_cast<std::size_t>(last_i)]);
        }
        if (differs) declare(&was[static_cast<std::size_t>(p)], &now[i]);
        last_i = static_cast<int>(i);
        last_p = p;
    }
    for (std::size_t p = 0; p < was.size(); ++p)
        if (!kept[p]) declare(&was[p], nullptr);   // a cue that left
}

// THE LANE'S HOVER BETWEEN REBUILDS (the contract at the declarations,
// render.h; architect 2026-11-21, send FF).
bool cue_pass_holds(const CueKey& key) {
    for (const CueDraw& c : g_cue_pass_scratch.cues)
        if (c.key == key) return true;
    return false;
}

int cue_pass_hovered_cues() { return g_cue_pass_scratch.hovered; }

namespace {

// THE REGIONS OF THE CUES WHOSE WEIGHT AS PAINTED IS NOT `weight_of`'s NOW,
// into the scratch's `regions` as whole columns cut to the pass's, those
// that meet merged into one: each such cue's OWN EXTENT — its chip's
// painted reach round its exact place and its label's ink as cut, the hull
// of its visible intervals (empty where a later label covers it whole). A
// cue's extent is its layout's alone (its place, its segments, the later
// boxes), never its weight's, so the compare (cue_pass_hover_damage) and
// the repaint (cue_pass_sync_hover) find the same regions on one layout and
// one set of weights. `write` stores each such cue's weight as the one it
// is now painted at, keeping the count of the lit (`hovered`): the
// repaint's. AND ON THE COMPARE ROAD (`write` false; architect 2026-11-21,
// send FF), A CHANGED CUE WHOSE EXTENT IS EMPTY — none of its columns on
// the pass's, its ink just off an edge while the conservative cull keeps it
// in the layout (a pan that took it off mid-fade) — has its weight retired
// there to the one it has now, the count kept true: the cache's ink is cut
// to the pass's columns, so NO PIXEL OF THE CACHE DEPENDS ON THAT WEIGHT,
// and writing it at the tick changes nothing any frame shows. Without it
// such a cue declares no region, so no frame comes to write it, and the
// clock would walk the lane on every tick, never reaching the at-rest exit
// (GuiPaintHandler::lane_cues_rest). A cue with any pixel there is the
// repaint's alone. False where no region is declared.
bool cue_hover_regions(
        const std::function<double(const CueKey&)>& weight_of, bool write) {
    CuePassScratch& sc = g_cue_pass_scratch;
    std::vector<CueDraw>& cues = sc.cues;
    std::vector<std::pair<int, int>>& regions = sc.regions;
    regions.clear();
    const int n = static_cast<int>(cues.size());
    if (n == 0 || sc.lane.h <= 0 || sc.w <= 0) return false;
    const int     x0    = sc.x0;
    const int     w     = sc.w;
    const double  half  = static_cast<double>(cue_chip_hit_left_px());
    const double  reach = static_cast<double>(cue_chip_paint_reach_px());
    // THE BOXES THE SURFACE WAS CUT ON, laid again from the layout itself
    // (one walk, at the first cue that differs): the cut is this layout's by
    // construction, whatever last wrote the scratch's two vectors.
    std::vector<double>& lo = sc.lo;
    std::vector<double>& hi = sc.hi;
    bool laid = false;
    for (int i = 0; i < n; ++i) {
        CueDraw& c = cues[static_cast<std::size_t>(i)];
        const double now = weight_of ? weight_of(c.key) : 0.0;
        if (now == c.hover) continue;
        if (!laid) {
            lay_cue_boxes(cues, x0, half, lo, hi);
            laid = true;
        }
        const double X = static_cast<double>(x0) + c.x;
        double e0 = X - reach;
        double e1 = X + reach;
        cue_label_spans(i, lo, hi, sc.spans);
        if (!sc.spans.empty()) {
            e0 = std::min(e0, sc.spans.front().x0);
            e1 = std::max(e1, sc.spans.back().x1);
        }
        const int c0 = std::max(x0, static_cast<int>(std::floor(e0)));
        const int c1 = std::min(x0 + w, static_cast<int>(std::ceil(e1)));
        const bool pixels = c1 > c0;
        if (pixels) regions.emplace_back(c0, c1);
        // THE WEIGHT AS PAINTED IS WRITTEN by the repaint (`write`), and on
        // the compare road where the cue's extent has NO PIXEL on the
        // pass's columns: no pixel of the cache depends on that weight.
        if (write || !pixels) {
            sc.hovered += (now != 0.0 ? 1 : 0) - (c.hover != 0.0 ? 1 : 0);
            c.hover = now;
        }
    }
    if (regions.empty()) return false;
    // Neighbors' regions that meet are one region: no column is cleared and
    // painted twice, nor declared twice.
    std::sort(regions.begin(), regions.end());
    std::size_t m = 0;
    for (std::size_t k = 1; k < regions.size(); ++k) {
        if (regions[k].first <= regions[m].second)
            regions[m].second = std::max(regions[m].second, regions[k].second);
        else
            regions[++m] = regions[k];
    }
    regions.resize(m + 1);
    return true;
}

}  // namespace

void cue_pass_hover_damage(
        const std::function<double(const CueKey&)>& weight_of,
        std::vector<GuiRect>& damage) {
    if (!cue_hover_regions(weight_of, /*write=*/false)) return;
    const GuiRect lane = g_cue_pass_scratch.lane;
    for (const std::pair<int, int>& r : g_cue_pass_scratch.regions)
        damage.push_back(GuiRect{r.first, lane.y, r.second - r.first, lane.h});
}

void cue_pass_sync_hover(cairo_surface_t* surface, int origin_y,
                         const std::function<double(const CueKey&)>& weight_of,
                         std::vector<GuiRect>& damage) {
    if (surface == nullptr) return;
    if (!cue_hover_regions(weight_of, /*write=*/true)) return;
    CuePassScratch& sc = g_cue_pass_scratch;
    const std::vector<CueDraw>& cues = sc.cues;
    const std::vector<double>&  lo   = sc.lo;
    const std::vector<double>&  hi   = sc.hi;
    const GuiRect lane  = sc.lane;
    const int     x0    = sc.x0;
    const double  reach = static_cast<double>(cue_chip_paint_reach_px());

    cairo_t* cr = cairo_create(surface);
    // CLIENT COORDINATES ON THE SURFACE, the rebuild's one translation
    // (FlagCache's head), so the direct road takes the context as the
    // rebuild's pass did.
    cairo_translate(cr, 0.0, -static_cast<double>(origin_y));
    for (const std::pair<int, int>& r : sc.regions) {
        const int c0 = r.first;
        const int c1 = r.second;
        cairo_save(cr);
        cairo_rectangle(cr, static_cast<double>(c0),
                        static_cast<double>(lane.y),
                        static_cast<double>(c1 - c0),
                        static_cast<double>(lane.h));
        cairo_clip(cr);
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_restore(cr);
        // Every cue whose extent meets the region — its chip's reach and
        // its label's whole box, the widest its ink can stand — painted
        // again in the pass's order; a cue outside it lays nothing here.
        paint_cue_ink(cr, lane, x0, cues, lo, hi, [&](int i) {
            const CueDraw& c = cues[static_cast<std::size_t>(i)];
            const double X  = static_cast<double>(x0) + c.x;
            const double e0 = std::min(X - reach,
                                       lo[static_cast<std::size_t>(i)]);
            const double e1 = std::max(X + reach,
                                       hi[static_cast<std::size_t>(i)]);
            return e1 > static_cast<double>(c0) &&
                   e0 < static_cast<double>(c1);
        });
        cairo_restore(cr);
        damage.push_back(GuiRect{c0, lane.y, c1 - c0, lane.h});
    }
    cairo_destroy(cr);
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
// THE BARS' HALO IS SIZED FOR THE KEY'S TOP GROW (render.h's THE PLATE'S
// BARS: the stored range covers every grow the key can name, so the plate's
// payload never depends on the live grow).
static_assert(kWaveformBarHaloSubcells == kWaveformGrowMaxQuarters,
              "the plate's stored bars cover the waveform_grow key's top");
// THE FADE'S START STANDS IN [1, 2) DEVICE PX (render.h's THE FADE BAND: the
// band ends where the bars change level, so a start at the switch width
// would be a band of no length; under 1 px it names a bar width no rung
// draws).
static_assert(kWaveformFadeHundredths >= 100 &&
                  kWaveformFadeHundredths < 100 * kWaveformSwitchPx,
              "the plate's fade starts in [1, 2) device px");

void   set_gui_scale_percent(int percent) { g_gui_scale_percent = percent; }
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
void set_palette_source_alpha(cairo_t* cr, GuiColor c, double alpha) {
    const GuiColor d = display_color(c);
    cairo_set_source_rgba(cr, d.r, d.g, d.b, alpha);
}

// -- The active palette (the contract is at its declaration, render.h) -----

namespace {
    // Construction state, never painted: gui_main installs the palette
    // before the window exists (the gui_scale shape).
    GuiPalette        g_palette{};
    uint64_t          g_palette_generation = 0;
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
    // The window's words (the struct's declaration, render.h), the row's
    // two tones.
    return WaveformPlateInks{
        display_transform::display_rgb(kArcDarkTones.waveform_ink),
        display_transform::display_rgb(kArcDarkTones.waveform_core)};
}

void install_palette() {
    // The contract is at the declaration (render.h): every member off the
    // row (kArcDarkTones, gui_theme.h) through the fill table
    // (palette_file.h), which covers the struct exactly.
    for (const GuiPaletteFill& f : kGuiPaletteFill)
        g_palette.*(f.member) = hex(kArcDarkTones.*(f.tone));
    ++g_palette_generation;
}

void install_true_colors(bool on) {
    // The contract, the caches it rebuilds and the apply shape the caller
    // owes are at the declaration (render.h).
    display_transform::set_true_colors(on);
    ++g_palette_generation;
}

// -- The flag editor's unrolled box ---------------------------------------

// WHERE ONE SEGMENT'S BOX STANDS on the committed label, as a real offset
// from the marker's exact place — the bound editor's anchor, and the whole of what
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
static double committed_cell_seam_off(const AppState& app,
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
    const double widths[3] = {run.width_px, cl.lower_w, cl.upper_w};
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
    const int u      = cue_editor_border_px();    // the field's border
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

    // THE RUN'S OWN WIDTH, UNROUNDED (architect 2026-11-15, the no-grid arc:
    // the box is its text's exact width and its two pads, a real length).
    const double run_w = run.width_px;
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
    const double box_w =
        static_cast<double>(pad_l) + run_w + static_cast<double>(pad_r);

    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    // THE EDITOR STANDS ON THE MARKER'S EXACT PLACE (architect 2026-11-15,
    // the no-grid arc), the very place the cue pass lays the resting label
    // on (painted_x_of_source_frame_on_basis, the column's exact sibling on
    // the same item basis): its box, its field text, its caret and the
    // cells riding its right edge stand where the label's segments stand, to
    // the fraction, so AT THE OPEN NOTHING UNDER THE FIELD MOVES, and the
    // segments left of the edited one, the flag pass's, stand on the same
    // place.
    const double mark_x = painted_x_of_source_frame_on_basis(
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
    const double anchor_off = committed_cell_seam_off(
        app, font, phase, idx, field_cell, iteration_on);

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
    // so nothing of the field paints past the waveform's columns. A field cut off at an edge is READ BY
    // Esc, A PAN AND A REOPEN (architect 2026-10-25): the marker-lane editors
    // are modals since that day, so nothing pans while one stands, and the
    // open does not pan to it either — the field travels with its marker,
    // the same answer the row gives for a label half off the edge at rest.
    const double bx = static_cast<double>(area.x) + mark_x + anchor_off;

    // The text VIEWPORT inside the box: the band the run is clipped to, and the
    // width the view offset is measured against. The caret column belongs to it
    // (a caret at the end must be inside the clip to be seen), and THIS IS
    // WHERE THE BORROW IS SPENT, on every kind: the viewport reaches one column
    // INTO the right pad — the column the box above deliberately does not buy —
    // so a caret at end-of-text is inside the clip with the run standing
    // exactly where the committed text stands. The caret one W (held to the
    // pad), the borrow takes those columns of the pad and the viewport ends
    // the rest of the pad short of the box's right edge.
    const double view_x0 = bx + static_cast<double>(pad_l);
    const int view_pad_r = std::max(pad_r - caret_px, 0);
    const double view_x1 = bx + box_w - static_cast<double>(view_pad_r);
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
    // WHAT IT HAS TO DO HERE IS NOTHING: the box holds this field's WHOLE
    // run by construction — two pads and the run's own width, unrounded, no
    // cap — so the viewport is exactly the run and the caret's column, the
    // travel's bound is zero and the origin stands at the viewport's start
    // at every caret position (the arithmetic below answers 0 but for the
    // sum's last bit). It stays as the field's one statement of where its
    // text begins, the contract State::view_offset_px carries.
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
    const double baseline = static_cast<double>(lane.y) + cue_baseline_px();

    // THE MARKER'S OWN STATE for the segments riding the field's right edge,
    // which keep their resting look: the inactive label on a disabled
    // marker and a tie follower's cells (2026-09-19), else the text tone — struck on a coincident
    // marker (below).
    const bool dis = phase ? pmv[static_cast<size_t>(idx)].disabled
                           : effective_disabled(mv, idx);
    // THE MARKER'S STRIKE (resolve_flag_face's table, 2026-10-26), off the
    // same memoized red-flag set the flag pass reads for the warp column (the
    // flag cache's rebuild, waveform_cache.cpp), so the riding segments and
    // the resting label agree on it — disabled or not, as there; the FIELD
    // itself never strikes (show_cue_label's head). A phase reset is never
    // struck (architect 2026-11-17, anything goes on that column).
    const bool struck =
        !phase && warp_red_flag_set_cached(
                      app, audio.sample_rate(),
                      static_cast<long>(audio.total_frames()))
                          .red.count(idx) > 0;
    // THE FIELD IS GTK'S IN-PLACE CELL EDITOR (architect 2026-11-18 / 19,
    // send Z's design, built in send AA; render.h's EDITING paragraph): the
    // entry's fill `ruler_face` (arc_dark 404552) inside a one-W border of
    // the accent `cue`, square-cornered, the text and the caret the
    // chrome's `field_text` (the text tone) — on every state of the marker,
    // disabled included (the chip carries the state). WHY: the field is the
    // legibility guarantee ("when you're looking very closely at a label it
    // has to be legible"), and THE SELECTED SUBSTRING takes the chrome's
    // SELECTED PAIR, `selected_text` on `selected_fill` (on arc_dark the
    // accent under the lit print, the dialog editor's pair), the ordinary
    // highlight — the pair that exists "precisely to avoid this problem".
    // A COINCIDENT MARKER'S FIELD IS THE SAME FIELD, its text and caret the
    // field's own, unstruck; `struck` marks its riding segments alone.
    const GuiPalette& pal = palette();
    const GuiColor field_fill    = pal.ruler_face;
    const GuiColor field_border  = pal.cue;
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
    //    EDITOR'S BORDER (render.h's cue_editor_border_px, one W, 2 device
    //    px on the tablet) in the accent round it, outside the pads on the
    //    left and right and on the lane's first and last rows, the entry's
    //    fill inside, square-cornered. IT PAINTS OVER THE LANE — the
    //    triangle's right edge and any neighbouring label it reaches
    //    (architect 2026-10-09: "it may overlap with the flag and other
    //    elements, that's fine"). A refused Enter recolors nothing
    //    (text_editor::refuse selects the whole text; the owner's card says
    //    why). Since the editor opens on any store index
    //    (enter_top_flag_edit), disabled included, a disabled marker's field
    //    is this same box, never the inactive grey.
    // ON ITS REAL EDGES (architect 2026-11-15, the no-grid arc): the box's
    // left and right edges are the marker's exact place plus real offsets,
    // its rows the lane's whole rows, both fills at exact coverage (the
    // exact fill, render.h) over the lane — the lane's face and the cues the
    // flag blit laid, opaque under every pixel the box can reach (its rows
    // are the lane's alone: the frame's line under the lane and the canvas
    // under that are never touched), inside the one clip rectangle set
    // above.
    const GuiRectF field_box{bx - static_cast<double>(u),
                             static_cast<double>(lane.y),
                             box_w + static_cast<double>(2 * u),
                             static_cast<double>(fill_h)};
    paint_cell_rect(cr, field_box, field_border);
    paint_cell_rect(cr,
                    GuiRectF{field_box.x + u, field_box.y + u,
                             field_box.w - 2 * u, field_box.h - 2 * u},
                    field_fill);

    // THE CARET AND THE SELECTION BAND ARE THE TEXT'S LINE BOX ROWS
    // (architect 2026-11-18 / 19, send Z's design, built in send AA: the
    // dialog editor's rule, one look for both editors; render.h's EDITING
    // paragraph): read off the one derivation (cue_line_box_top_px,
    // cue_line_box_h_px, render.h), never re-derived here — on the tablet
    // rows 2 .. 29 under the lane's 32, the box's whole inner 14 W inside
    // the 2-row border.
    const double band_y = static_cast<double>(lane.y) + cue_line_box_top_px();
    const double band_h = cue_line_box_h_px();

    // NO CLIP STANDS ROUND THE TEXT (architect 2026-11-15, the no-grid arc):
    // the viewport is exactly the run and the caret's column (the box is the
    // run's own width and its pads), so the run, its selection and its caret
    // lie inside the pads by construction and the waveform's columns' clip
    // above — one whole rectangle — is the only clip the exact fills below
    // stand under.

    // 2. The selection highlight, then 3. the text — THE CHROME'S SELECTED
    //    PAIR on the band over the field pair (`field_ink` on `field_fill`,
    //    set at the state block above).
    //
    //    THE BAND'S TWO EDGES STAND ON THEIR BYTES' EXACT x — the run's own
    //    pen at each boundary (byte_x), unrounded, filled at exact coverage
    //    — so the highlight cannot drift off the glyphs it marks however
    //    proportional they are. A selection whose glyphs carry no advance
    //    still marks one px.
    //
    //    THE TEXT'S COLOR CHANGES AT THOSE SAME BOUNDARIES, GLYPH BY GLYPH
    //    (the dialog editor's rule, paint_modal_dialog): a selection's edge
    //    is a byte boundary, between two glyphs, so the run is painted as
    //    three byte ranges — before the band in the field text, the band's
    //    in the selected text, after it in the field text — each range
    //    whole glyphs AT THE PLACES THE WHOLE RUN GIVES THEM
    //    (text_shape::show_shaped_run_bytes: never a run shaped from the
    //    substring alone, which could kern its first glyph differently),
    //    with no clip at the band — nothing fractional and nothing
    //    multi-rect under the exact fills. A glyph that overhangs the
    //    boundary beside it keeps its own ink there.
    const bool has_sel = text_editor::has_selection(ed);
    const size_t s0 = static_cast<size_t>(text_editor::selection_start(ed));
    const size_t s1 = static_cast<size_t>(text_editor::selection_end(ed));
    if (has_sel) {
        const double sx0 = text_origin_x + byte_x[s0];
        const double sx1 = text_origin_x + byte_x[s1];
        paint_ink_rect(cr,
                       GuiRectF{sx0, band_y, sx1 > sx0 ? sx1 - sx0 : 1.0,
                                band_h},
                       sel_fill);
    }
    set_palette_source(cr, field_ink);
    if (!has_sel) {
        text_shape::show_shaped_run(cr, run, text_origin_x, baseline);
    } else {
        text_shape::show_shaped_run_bytes(cr, run, text_origin_x, baseline, 0,
                                          s0);
        text_shape::show_shaped_run_bytes(cr, run, text_origin_x, baseline,
                                          s1, ed.pending.size());
        set_palette_source(cr, sel_text);
        text_shape::show_shaped_run_bytes(cr, run, text_origin_x, baseline,
                                          s0, s1);
    }

    // 4. The caret: a blink-gated one-W line FROM THE CURSOR'S OWN BYTE
    //    BOUNDARY'S EXACT x on the band's exact rows, at exact coverage. IT
    //    IS INK, NOT FIELD, so it stays the box's TEXT color, `field_ink`,
    //    wherever it lands (architect 2026-10-03) — a caret that changed
    //    color on crossing a selection edge would be stating something about
    //    the selection rather than about the cursor. (It stands at the
    //    band's edge, never inside it.)
    if (text_editor::cursor_visible_now(ed))
        paint_ink_rect(cr,
                       GuiRectF{text_origin_x + caret_off, band_y,
                                static_cast<double>(caret_px), band_h},
                       field_ink);

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
    // in the sidebar text or the inert label, struck on a coincident marker —
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
        // The marker lane's last column, one past it: the waveform area's
        // (the lane's face stands on the area's columns, the panel's whole
        // width, render.h's well_columns; this painter's clip holds the
        // same columns).
        const int well_end = area.x + area.w;
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
        const double gap    = static_cast<double>(cue_segment_gap_px());
        const double run_x0 = bx + box_w;
        // THE RUN IS NOT CLIPPED (architect 2026-10-10 ~19:00): the riding
        // cells paint where the paint order puts them. The editor paints
        // after the lane pass, so the run stands over a neighbour's label and
        // triangle as the field itself does (render.h's EDITING paragraph,
        // "it may overlap with the flag and other elements, that's fine").
        double cursor_x = run_x0;
        const double lower_seam = cursor_x;
        // The field's border columns stand in the first gap's first columns
        // (field_box reaches one border past the pads), so the riding face
        // starts after it: the run stays where the resting label's is and the
        // border is never painted over.
        const double face_x0 = run_x0 + static_cast<double>(u);
        const auto ride = [&](const text_shape::ShapedRun& seg_run,
                              double seg_w) {
            const double x0 = cursor_x + gap;
            const double w  = static_cast<double>(pad_l) + seg_w +
                              static_cast<double>(pad_r);
            // The face box over the gap and the segment, as the resting
            // label's one box covers its gaps — the lane's flat `lane_face`
            // (paint_cues' box) through the lane's columns, the ground past
            // them, which the painter's clip (the waveform's columns, above)
            // cuts, so nothing lands past the lane's columns. ON REAL EDGES (the no-grid arc,
            // 2026-11-15): the box's two edges are the field's right edge
            // plus real offsets, filled at exact coverage over the lane's
            // cues — an opaque box laid at its real edges hides a
            // neighbour's label exactly from there on.
            const double fx = std::max(cursor_x, face_x0);
            const double fx1 = x0 + w;
            const double lane1 = std::clamp(static_cast<double>(well_end), fx,
                                            std::max(fx, fx1));
            if (lane1 > fx)
                paint_cell_rect(cr,
                                GuiRectF{fx, static_cast<double>(lane.y),
                                         lane1 - fx,
                                         static_cast<double>(fill_h)},
                                pal.lane_face);
            if (fx1 > lane1)
                paint_cell_rect(cr,
                                GuiRectF{lane1, static_cast<double>(lane.y),
                                         fx1 - lane1,
                                         static_cast<double>(fill_h)},
                                pal.face);
            const double tx = x0 + static_cast<double>(pad_l);
            // The resting label's ink: the inert label `text_dim`, else the
            // sidebar text `sidebar_text` (resolve_flag_face's table),
            // struck on a coincident marker — at its exact pen, uncut (the
            // riding run stands over whatever the lane holds).
            show_cue_label(cr, seg_run, tx, baseline,
                           cell_dis ? pal.text_dim : pal.sidebar_text, struck,
                           nullptr, 0);
            cursor_x = x0 + w;
        };
        // Both handles stand: either ride needs cl.present.
        if (ride_lower) ride(*cl.lower_run, cl.lower_w);
        const double upper_seam = cursor_x;
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
        // triangle stays the flag pass's. It begins after the field's border
        // column, as its face does: that column is the field's, claimed by
        // `box`. It ends where the pixels end.
        const double ride_w = cursor_x - face_x0;
        if (cursor_x > run_x0 && ride_w > 0.0) {
            FlagHitRect& r = out.riding_cells;
            r.marker_index          = idx;
            r.x                     = face_x0;
            r.y                     = static_cast<double>(lane.y);
            r.w                     = ride_w;
            r.h                     = static_cast<double>(fill_h);
            r.iter_lower_boundary_x = lower_seam;
            r.iter_upper_boundary_x = upper_seam;
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
    // its border included.
    //
    // AND IT IS CLIPPED TO THE WAVEFORM'S COLUMNS as its pixels are (the clip
    // above): a field cut off at the last column claims its visible part, and
    // a field wholly past the edge publishes an empty box, which contains no
    // point.
    // THE PUBLISHED BOX IS THE FIELD'S REAL EDGES (architect 2026-11-15, the
    // no-grid arc) cut to the columns, over the lane's whole rows — the
    // pointer's real place is what asks it. (The editor's damage is the top
    // strip's, whole: Viewport::invalidate_top_strip.)
    {
        const double x_lo =
            std::max(field_box.x, static_cast<double>(area.x));
        const double x_hi = std::min(field_box.x + field_box.w,
                                     static_cast<double>(area.x + clip_w));
        out.box = GuiRectF{x_lo, static_cast<double>(lane.y),
                           std::max(x_hi - x_lo, 0.0),
                           static_cast<double>(fill_h)};
    }
    out.text_origin_x = text_origin_x;
    out.byte_x        = std::move(byte_x);
}
