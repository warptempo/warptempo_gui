#pragma once
#include "warpmarkers.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment for target-view waveform

#include <cairo/cairo.h>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

class GuiAudio;
struct AppState;
struct DragOverlay;

struct GuiRect {
    int x;
    int y;
    int w;
    int h;
};

struct GuiColor {
    double r;
    double g;
    double b;
};

// Build a GuiColor from a 0xRRGGBB hex literal, converting each 8-bit
// channel to an exact [0,1] double. constexpr so palette constants stay
// compile-time. RGB only (the renderer uses cairo_set_source_rgb); if an
// alpha channel is ever needed, add a separate 0xRRGGBBAA overload rather
// than widening this one.
inline constexpr GuiColor hex(uint32_t rgb) {
    return GuiColor{
        static_cast<double>((rgb >> 16) & 0xFF) / 255.0,
        static_cast<double>((rgb >>  8) & 0xFF) / 255.0,
        static_cast<double>( rgb        & 0xFF) / 255.0,
    };
}

// Trim boundaries in domain-frame samples (source-frame in source view,
// target-frame in target view). Trim no longer dims any renderer — it is
// consumed only by render_trim_stems to place the two boundary stems. Values
// are the AUTHORED positions from compute_displayed_trim (paint_handler.cpp):
// per-bound, unordered (bounds may be inverted mid-gesture — crossed cannot
// rest — and this paints per frame; past-EOF is load-fatal, so each bound is
// within [0, EOF]); each stem is placed independently, so no order is
// assumed here.
struct TrimRange {
    int64_t begin;
    int64_t end;
};

// Palette: bases shared across the renderer module and
// main.cpp.
inline constexpr GuiColor kBackground       = hex(0x1A1A1F);
inline constexpr GuiColor kWaveform         = hex(0x8CBFE6);

// Out-of-trim waveform sample color. Applied by on_redraw as a
// CAIRO_OPERATOR_ATOP overlay over the out-of-trim sample pixels of the
// blitted plate — NOT baked into the plate, which is trim-agnostic. ATOP
// uses the plate's alpha as the mask, so only painted sample pixels are
// recolored (gaps stay background) at the exact tuned RGB, no blend. This
// is a standalone tunable color, NOT a global dim factor — the global
// out-of-trim dim was retired and stays retired; nothing but the sample
// pixels dim. Default is roughly kWaveform blended ~55% toward kBackground
// (still clearly a waveform, just faded). Tune by eye/ear in the car loop.
inline constexpr GuiColor kWaveformDimmed   = hex(0x4D6378);

// Shared overlay wash, used by BOTH the phase reset overlay (waveform /
// target view) and the trim pair-drag chip-row band (top strip). A flat
// translucent fill composited at a small alpha so it lifts whatever it
// covers (background, waveform, and already-dimmed out-of-trim pixels alike)
// — polarity-opposite to the out-of-trim dim, which darkens. The phase reset
// overlay is a rectangle spanning forward in target time from the focused
// phase reset marker — the stretch of output immediately following the reset
// over which the re-seeded phase takes hold before normal propagation
// resumes. GuiColor carries no alpha, so the alpha is the separate constexpr
// double below.
//
// Flat fill, not a plate-masked recolor: a recolor was tried and rejected
// because it goes invisible on the silent stretches where phase resets often
// sit, whereas a flat fill keeps the span legible everywhere. Pale azure
// rather than white: the dark theme's kBackground (0x1A1A1F) is blue-cast, and
// white's neutral lightening pulls the covered pixels toward gray, which reads
// greenish by contrast against the surrounding blue-cast dark; a color in the
// kWaveform / kSelected hue family preserves the background's blue cast
// instead of neutralizing it. A tinted source lifts less per alpha unit than
// white (the per-channel add is alpha times source-minus-dest). Both values
// tuned by eye on the panel.
inline constexpr GuiColor kOverlay      = hex(0xB8D4F0);
inline constexpr double    kOverlayAlpha = 0.05;

// The pair-drag band's ring strength — the ring paints in kOverlay at this
// alpha (the band's own hue, stronger than the kOverlayAlpha wash), the
// outline-palette relationship (a brighter sibling of the fill) expressed
// in alpha; the tuning knob.
inline constexpr double    kOverlayOutlineAlpha = 0.30;

// Region-select wash — the alpha brightening painted over the region span, the
// visual counterpart of the out-of-trim dim (that darkens the samples; this
// brightens the span, Ableton-style). A flat translucent fill (not a plate
// masked recolor) over the full waveform height, composited AFTER the plate and
// the out-of-trim dim, so a region inside a dimmed area lifts the dimmed pixels
// — accepted, it stays visible. A lightened kWaveform tone, kept in the same
// blue-cast hue family as kOverlay so the lift reads as brightening rather than
// graying (see the kOverlay note). Both values architect-tunable on the labwc
// pass; start subtle.
inline constexpr GuiColor kRegionWash      = hex(0xC7DEF5);
inline constexpr double    kRegionWashAlpha = 0.10;

inline constexpr GuiColor kMarker           = hex(0x9145AD);
inline constexpr GuiColor kSelected         = hex(0x3DAEE9);  // Breeze blue
inline constexpr GuiColor kPlayheadScanner  = hex(0xF2D959);
inline constexpr GuiColor kPlayheadCursor   = hex(0x1ABC9C);  // green cursor
inline constexpr GuiColor kAccent           = hex(0xBF332E);
inline constexpr GuiColor kText             = hex(0xFCFCFC);  // Breeze paper white

// The chip outline palette — a brighter sibling of each fill
// (kMarker / kSelected / kAccent / kTrimMarker). Painted as the solid 1px
// outline ring around a chip (see EditorTextBox::outline / kChipOutlinePx);
// these are the tuning knobs.
inline constexpr GuiColor kMarkerOutline    = hex(0xC178E0);
inline constexpr GuiColor kSelectedOutline  = hex(0x7DCDF7);
inline constexpr GuiColor kAccentOutline    = hex(0xE5655F);
inline constexpr GuiColor kTrimMarkerOutline = hex(0xFFA040);

// Single dimming factor for a DISABLED marker, applied uniformly across its
// flag (chip ring + fill + glyphs), its half-triangle, and — for the
// last-selected marker — its stem. Stems paint only for the last-selected
// marker, so omission can no longer signal disablement — this alpha is the sole
// disabled cue. Selection controls the color class (kSelected family) and, for
// the anchor, the stem reveal; disablement controls this alpha, and the two
// compose. Architect-tunable.
inline constexpr double kDisabledMarkerAlpha = 0.45;

// Trim boundary stem color (#F67400 orange). Distinct from
// kMarker, kSelected, the teal cursor, and the yellow scanner. A set
// trim begin/end always paints as a vertical stem in this one color —
// trim is outside the selection system, so there is no selected variant.
inline constexpr GuiColor kTrimMarker       = hex(0xF67400);  // Breeze orange

// -- GUI font size ---------------------------------------------------------
//
// The single GUI-wide monospace text size is the font_size setting, a plain
// number of points at the conventional 96 DPI. The current value lives as
// file-scope state in render.cpp beside the monospace grid metrics; the
// file-load and settings-editor application points push it through
// set_gui_font_size_pt. Everything in the two strips scales proportionally
// via gui_font_scale() = font_size / kDefaultFontSizePt, so at the default
// (11) every derived quantity equals its former fixed constant exactly.
inline constexpr double kDefaultFontSizePt = 11.0;

// Set the current GUI font size (points). The setter only records the
// value; the geometry re-measure happens on the next redraw via
// init_monospace_grid_metrics, and the callers route the cache rebuild
// through the same path a window resize uses.
void set_gui_font_size_pt(double pt);

// Proportional scale factor s = font_size / 11. Exactly 1.0 at the default.
double gui_font_scale();

// Text pixel size handed to cairo: font_size * 96 / 72, carried as an exact
// double (points -> pixels at the conventional 96 DPI; warptempo_gui does
// not support HiDPI). Text is the only thing that renders at fractional
// sizes — every other scaled quantity below rounds to an integer. At the
// default this is 11.0 * 96.0 / 72.0, the former kFlagFontSize constant.
double flag_font_size_px();

// Flag chip internal padding around the text glyph bounding box, split per
// axis so the two can be tuned independently. Both are the single source of
// truth for their axis — every chip renderer and the hit-rect computation must
// read these, never a literal. Each is the authored value (1 / -1) scaled by
// gui_font_scale() and rounded with std::nearbyint so it stays an integer:
// the aliased plus-point-five sharp-edge convention for 1 px strokes and
// integer-edged rects keeps holding at every size. At scale 1 each equals
// its authored value by identity (nearbyint(1*1) == 1, nearbyint(-1*1) == -1).
//
// flag_pad_x_px sets chip WIDTH: the painted fill and the hit rect both span
// glyph_advance + 2*flag_pad_x_px() + 2*kChipOutlinePx (the outline ring sits
// outside the padding).
//
// flag_pad_y_px sets chip HEIGHT via the row metric: the row height is
// font (ascent+descent) + 2*flag_pad_y_px() + 2*kChipOutlinePx, and the
// baseline offset is flag_pad_y_px() + kChipOutlinePx + ascent. The authored
// pad_y is NEGATIVE (-1) by deliberate design: the measured cairo font band
// (ascent+descent) carries internal leading, so at pad_y = -1 the row metric
// is round(font_height - 2) + 2*kChipOutlinePx = font_height at scale 1 — the
// outline ring overlaps the band's outermost row (top and bottom), eating the
// empty leading, not glyph ink. The cursor and selection highlight (which span
// the band) are clamped to the fill interior so they stay inside the ring;
// only the antialiased glyph text may reach into those blank leading rows.
inline double flag_pad_x_px() { return std::nearbyint(1.0 * gui_font_scale()); }
inline double flag_pad_y_px() { return std::nearbyint(-1.0 * gui_font_scale()); }

// The solid outline ring outside the chip padding: part of the chip rect and
// the row metric (a chip is outline + pad + glyph ink). The single width knob;
// baked into flag_chip_rect and the monospace row/baseline metrics so every
// derived surface (strip heights, stem overhang, baseline solves, hit rects)
// tracks it automatically.
inline constexpr int kChipOutlinePx = 1;

// The glyph-run inset from a chip's left edge: the outline ring plus the left
// inner pad (kChipOutlinePx + flag_pad_x_px()). The ONE value every chip
// anchor/caret site uses to place the glyph origin relative to the chip's left
// edge (where flag_chip_rect's r.x lands — for marker flags the left-anchored
// position on the marker's pixel column). Every renderer passes
// anchor_x = text_left + flag_glyph_inset_px() and back-derives the chip edge
// via EditorTextBox::hl_pad, so paint and hit share one geometry.
inline double flag_glyph_inset_px() { return kChipOutlinePx + flag_pad_x_px(); }

// The outer (window-edge) gap between each strip's edge-most lane and the
// window edge, and the waveform-side gap between the innermost lane and the
// waveform. Stays a compile-time zero under the font_size scaling — zero is
// scale-invariant — so the lanes pack tight against the window edges and the
// waveform. The constant survives so the gap reappears structurally if it is
// ever un-zeroed (the strip lane-stack geometry in main.cpp carries it).
constexpr double kFlagBottomLiftPx = 0.0;

// Fixed-pixel mirrored strip lane grid. G is the single tunable inter-lane gap
// between each adjacent lane pair within a strip. One named constant, one
// place to change it. Now 0 — the lanes of each strip touch, and the
// waveform-side and outer (window-edge) gaps (both kFlagBottomLiftPx, also 0)
// vanish, so lanes and strips pack tight against each other and the window
// edges. Stays a compile-time zero under font_size scaling — zero is
// scale-invariant.
constexpr double kRowGapPx = 0.0;

// Defensive window floor (a conservative 640x480 minimum). Enforced two ways:
// the Wayland set_min_size hint at toplevel creation, and an internal clamp in
// the geometry helpers so the waveform arithmetic is always valid regardless of
// what the compositor sends. Not sized to fit content — the longest dialogue
// may clip at the floor, which is acceptable (nobody authors at 640x480).
constexpr int kMinWindowWidthPx  = 640;
constexpr int kMinWindowHeightPx = 480;

// Authored pixel geometry of a marker flag (architect spec), scaled by
// gui_font_scale(), rounded with std::nearbyint, floored to a sane minimum.
// The flag is a RECTANGLE two pixels taller than wide (a slight upright
// rectangle) that carries a tip-down TRIANGLE directly beneath it; the tip
// marks the frame. These are the values at the default font size.
inline constexpr int kFlagWidthPx  = 17;
inline constexpr int kFlagHeightPx = 19;

// The flag rectangle's painted width / height for the current font size. The
// TRIM b/e chips are textless rectangles of this exact width/height too. Both
// carry a >= 5 px floor so a tiny font still leaves a usable, outline-able
// shape.
inline int flag_lane_w_px() {
    const int w = static_cast<int>(std::nearbyint(
        static_cast<double>(kFlagWidthPx) * gui_font_scale()));
    return w < 5 ? 5 : w;
}
inline int flag_lane_h_px() {
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kFlagHeightPx) * gui_font_scale()));
    return h < 5 ? 5 : h;
}

// Height H (px) of the code-generated tip-down triangle mask, SHARED by the
// playhead cursor and every marker/trim flag (one mask builder). The mask
// width is 2*H - 1 (odd by construction, so it centers exactly on the 1 px
// column). H is DERIVED from the flag width so the triangle sits one pixel
// inside each flag-rectangle edge: the flag triangle width kFlagWidthPx-2 = 15
// gives H = (15+1)/2 = 8 at scale 1 (the odd-row rule — every triangle row is
// odd, top row 15 down to a 1 px tip). Clamped to at least 2 so the mask
// always has a tip row below a top row. The half-width below derives from H,
// so the two can never drift.
inline int playhead_triangle_h_px() {
    const int h = (flag_lane_w_px() - 1) / 2;
    return h < 2 ? 2 : h;
}

// The cached cairo A8 mask surface for the tip-down triangle (W = 2H-1 by H,
// binary alpha, row y spanning columns y..W-1-y). Owned by render.cpp
// file-scope state beside the grid metrics; regenerated when H changes. The
// same mask stamps the playhead cursor triangle and each marker/trim flag's
// triangle (centered on the column, tip at the waveform top edge). Never null.
cairo_surface_t* playhead_triangle_mask();

// Waveform-internal top/bottom inset, in pixels. The drawn waveform samples
// are confined to [area.y + waveform_inset_px(), area.y + area.h -
// waveform_inset_px()] so the waveform is symmetric about its area center and
// the marker/trim stems have a clean stem-only band at the top before the
// samples begin. Equal to the triangle mask height by construction. (The
// cursor triangle no longer sits inside this band — it moved to its own
// triangle lane above the waveform — so the former triangle-clearance
// rationale retires; the symmetric-margin purpose remains.)
inline int waveform_inset_px() { return playhead_triangle_h_px(); }

// Half-width (px) of the tip-down triangle's horizontal footprint, H - 1
// (the mask is 2H-1 wide, centered on the column); bounds the playhead's
// off-screen cull and its invalidation strip. Single definition shared by
// render.cpp (cull) and main.cpp (invalidation). At scale 1 it is 7.
inline int playhead_half_px() { return playhead_triangle_h_px() - 1; }

// Pre-first-paint fallback for the measured monospace row height and baseline
// offset (Liberation Mono at the DEFAULT 11 pt — these stay compile-time and
// assume the default font size; they only seed geometry before the first
// measure and are overwritten immediately). on_resize can fire before the
// first redraw measures the real font; these seed the geometry so it is sane
// (never a negative waveform) until init_monospace_grid_metrics overwrites
// them. The -1.0 term is the authored flag_pad_y_px value at scale 1, the 1.0
// term is kChipOutlinePx (round(18 - 2) + 2*1 = 18; baseline -1.0 + 1.0 + 14.0).
constexpr int    kRowHFallbackPx       = 18;
constexpr double kRowBaselineOffFallbackPx =
    -1.0 + 1.0 + 14.0;

// Forward declaration: defined with its full doc comment below. Needed here
// because flag_chip_rect (inline) reads the row height.
int monospace_row_h();
// Forward declaration: defined with its full doc comment below. Needed here
// because flag_chip_rect (inline) reads it.
double monospace_row_baseline_offset();
// Forward declaration: defined with its full doc comment below. Needed here
// because flag_chip_rect (inline) computes the chip width from it.
double monospace_advance();

// Total chip width (px) for a glyph_count-glyph chip: the padded glyph advance
// plus the outline ring on both sides. This is the ONE definition of a chip's
// width — flag_chip_rect's r.w below reads it, so a chip's painted and
// hit-tested width match with no drift. The advance is the cached monospace
// arithmetic (glyph count times monospace_advance()), exact for the ASCII chip
// strings and independent of any cairo context.
inline int flag_chip_width_px(size_t glyph_count) {
    const double pad = flag_pad_x_px();
    const double advance =
        static_cast<double>(glyph_count) * monospace_advance();
    return static_cast<int>(std::round(advance + 2.0 * pad))
        + 2 * kChipOutlinePx;
}

// Single source of truth for a text-box's painted/hit rectangle. The
// bottom-strip editors (settings / bpm / render-commit) derive their fill rect
// from this one function so paint and hit cannot drift. (Marker flags and trim
// chips are now fixed-width geometric shapes — see paint_flag_shape — and no
// longer route through here.)
//
// The returned rect is the TOTAL chip footprint INCLUDING the outline ring: it
// grows by kChipOutlinePx on every side relative to the padded glyph box. The
// fill insets by kChipOutlinePx inside it (render_editor_text_box). text_left is
// the box's left edge as the caller already positioned it: r.x = round(text_left).
// The fill starts one
// pixel right of the left edge, and the glyph run sits kChipOutlinePx + flag_pad_x_px()
// (= flag_glyph_inset_px()) inside the chip edge. The vertical growth is carried
// by monospace_row_h() / monospace_row_baseline_offset() (which now bake the
// outline in), so r.y / r.h below are unchanged — the top lifts and the height
// grows automatically.
//
// Width is computed from the cached per-character monospace advance
// (flag_chip_width_px -> monospace_advance(), measured once on the real paint
// surface at startup), NOT from a per-call cairo_text_extents — that was the
// residual edge bug: paint measured on the window surface, hit on a 1x1 scratch
// surface, and the integer width diverged by 1px. Chip text is ASCII-only, so
// glyph_count is the exact glyph count and glyph_count * monospace_advance() is
// the exact advance, identical at paint and hit, with zero surface dependence.
//
// Inputs:
//   text_left   - the chip's left edge as the caller positioned it (left-anchored
//                 on the marker column for marker flags; the trim anchoring for
//                 b/e chips). Chip left edge = round(text_left); the glyph run sits
//                 flag_glyph_inset_px() (ring + left inner pad) to the right of
//                 it, folded into where the caller places the glyph origin vs.
//                 text_left (see consumers).
//   glyph_count - number of glyphs in the chip's text (== text.length() for the
//                 ASCII chip strings). Width = flag_chip_width_px(glyph_count).
//   baseline_y  - the text baseline y the caller solved for its row (Lower row
//                 for regular/phase-reset chips, Upper for trim). The box top is
//                 baseline_y - monospace_row_baseline_offset(); the height is
//                 the full monospace_row_h() slot.
//
// Returns the integer GuiRect [x, y, w, h]; rounding happens here, once.
// Consumers use the returned ints directly — no consumer re-rounds or
// recomputes any edge.
inline GuiRect flag_chip_rect(double text_left, size_t glyph_count,
                              double baseline_y) {
    GuiRect r;
    r.x = static_cast<int>(std::round(text_left));
    r.y = static_cast<int>(std::round(
              baseline_y - monospace_row_baseline_offset()));
    r.w = flag_chip_width_px(glyph_count);
    r.h = monospace_row_h();
    return r;
}

// The former kFlagFontSize constant (11.0 * 96.0 / 72.0) is now the runtime
// accessor flag_font_size_px() declared above — same pt->px arithmetic,
// driven by the font_size setting instead of a fixed 11.

// Editor text-box primitive. Draws the full editable-text-box
// anatomy shared by the flag-payload editor (top strip) and the settings
// editor (bottom strip), in paint order: solid fill behind the editable
// region, optional static prefix, editable text, selection swap, and a
// blink-gated 1-px cursor. Killing the duplication between the two editors
// is the point — both callers differ only in the resolved fill color, the
// optional prefix, and the anchor.
//
// Geometry: `anchor_x` is the left edge of the prefix (or of the editable
// text when `prefix` is empty). The editable region paints at
// `anchor_x + prefix_advance`; the solid fill covers only the editable
// region (the prefix, if any, sits to its left on the canvas), via the
// shared flag_chip_rect helper. The box height is the cached
// monospace_row_h() (the same metric the strip rows use) and the top is
// `baseline_y - monospace_row_baseline_offset()`, so the box fills its full
// row slot — callers solve baseline_y so the box bottom lands at the slot
// bottom. The cursor uses the std::round(x)+0.5 half-pixel convention for a
// crisp single-pixel column. The cursor and the selection highlight span only
// the glyph ink band (ascent-to-descent) — NOT the full slot; only the step-1
// fill spans the full padded slot.
//
// Colors are pre-resolved by the caller: `fill` is the resolved chip
// color and `text_color` is kText. The selection swap fills the selected
// range with `text_color` and repaints the selected substring in `fill`
// for contrast.
//
// Step 1 always paints the outer kChipOutlinePx band of the chip rect
// (flag_chip_rect, which includes the ring) in `outline`, then fills the inner
// rect inset by kChipOutlinePx in `fill` — the solid outline ring around the
// box. The chips pass their state-dependent outline siblings; the bottom-strip
// editors pass kBackground for BOTH ring and fill (an invisible ring — the box
// reads as light text on the dark strip) and kAccent/kAccentOutline when
// red-flashing, exactly a parse-fail chip's colors. The cursor and the
// selection highlight span the glyph ink band, which sits inside the padding
// inside the outline, so both stay within the ring whenever visible.
struct EditorTextBox {
    double               anchor_x        = 0.0;
    double               baseline_y      = 0.0;
    std::string          prefix;            // optional; "" = none
    std::string          text;              // editable content
    // The glyph-run inset from the chip's left edge (ring + left inner pad =
    // flag_glyph_inset_px()). render_editor_text_box back-derives the chip edge
    // as editable_left - hl_pad, so hl_pad must equal the inset the caller used
    // to place anchor_x for the fill rect and the glyph run to coincide.
    double               hl_pad           = flag_glyph_inset_px();
    GuiColor             fill             = kMarker;
    GuiColor             text_color       = kText;
    bool                 has_selection    = false;
    int                  selection_start  = 0;
    int                  selection_end    = 0;
    bool                 cursor_visible   = false;
    int                  cursor_pos       = 0;
    GuiColor             outline          = kMarker;
};
// `alpha` (default opaque) dims the whole composited box uniformly — the
// disabled-marker cue for a chip (flag). It is applied by rendering the box
// into a group and compositing it with alpha, so the ring, fill, and glyphs
// dim together. Every non-chip caller (bottom-strip editors, trim chips) leaves
// it at 1.0 and paints byte-identically.
void render_editor_text_box(cairo_t* cr, const EditorTextBox& s,
                            double alpha = 1.0);

// Screen-coord rect of one rendered flag, keyed back to its marker index.
// Emitted in the same order flags appear left-to-right.
struct FlagHitRect {
    int    marker_index;
    double x;
    double y;
    double w;
    double h;
};

// All rendering helpers take a Cairo context and pixel-space rectangles; they
// have no X11 or event-loop dependencies.

void render_background(cairo_t* cr, int x, int y, int w, int h);

// Draws one channel's waveform into `area`, displaying samples in
// [viewport_start_sample, viewport_end_sample). When `warp_frame_map` is null
// (source view) the viewport range is interpreted in source-frame and
// each column reads `audio.get_peak_range` directly. When `warp_frame_map` is
// non-null (target view) the viewport range is target-frame: each
// column's [t0, t1) is translated to source-frame via
// `map_target_to_source` before the pyramid read, producing the
// deformed-waveform display.
//
// The plate paints uniformly in `color` — it is trim-agnostic. The
// out-of-trim dim is NOT baked here; on_redraw paints it as an ATOP
// overlay over the blitted plate (see kWaveformDimmed and
// compute_out_of_trim_rects), so a trim set/clear/drag never re-rasterizes
// these pixels.
void render_waveform(cairo_t* cr,
                     GuiRect area,
                     const GuiAudio& audio,
                     int channel,
                     long long viewport_start_sample,
                     long long viewport_end_sample,
                     GuiColor color,
                     const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr);

// Draws a thin 1px vertical line across `area` at column `playhead_pixel_x`
// (offset from area.x, float for subpixel centering). No-op if outside.
// The inverted-triangle indicator comes from the code-generated mask
// (playhead_triangle_mask(), cached in this module's file-scope state);
// it's stamped above the stem via cairo_mask_surface, tinted with `color`.
// The triangle belongs to the cursor exclusively under the split-playhead
// model; pass `draw_triangle = false` for the scanner call so only the
// vertical line is drawn.
//
// `ink_plate` (default null) is the displayed waveform plate — an ARGB32 image
// surface whose alpha is opaque exactly where a sample column was painted and
// transparent in the gaps. When non-null, the line is two-toned per-pixel: it
// stays `color` wherever its column crosses no waveform ink (background, the
// inter-channel gap, the inset bands, notches inside the waveform) and is
// overdrawn in kBackground wherever the column crosses an opaque sample pixel,
// cutting a dark notch through the waveform. When null, the line is the
// existing single-color stroke. The triangle is unaffected and always paints in
// `color` over the line.
void render_playhead(cairo_t* cr,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color,
                     bool draw_triangle = true,
                     cairo_surface_t* ink_plate = nullptr);

// Draws the LAST-SELECTED marker's vertical 1-pixel stem across `waveform_area`
// when its resolved sample falls inside [viewport_start_sample,
// viewport_end_sample). The stem spans the WAVEFORM AREA ONLY — top at
// `waveform_area.y`, bottom at the waveform bottom — with no strip overhang;
// the flag+triangle structure in the top strip provides the visual connection
// above, the triangle tip touching the stem's start at the waveform top edge.
// The stem paints for exactly `last_selected` (the active column's anchor
// index, or -1 for none) — no other marker grows a stem, though the blue flag
// highlight still marks the whole selection. A stem paints kSelected; a
// DISABLED last-selected marker's stem dims plainly under kDisabledMarkerAlpha
// (the marker's triangle lives in the flag structure now, a separate surface,
// so the stem and triangle no longer share pixels and no group composite is
// needed). Effective disabled state is computed inline from the marker list (a
// label reference inherits the disabled flag of its defining marker). The
// out-of-trim sample-pixel dim (on_redraw's ATOP overlay, kWaveformDimmed) is
// separate and does not touch stems.
// `warp_frame_map` (default null) shifts marker positioning into the mapped
// display domain (target view's live map): each marker's source-frame position
// is run through `map_source_to_target` before viewport clipping and column
// placement. Null warp_frame_map = identity.
void render_markers(cairo_t* cr,
                    GuiRect waveform_area,
                    const std::vector<GuiWarpMarker>& markers,
                    long long viewport_start_sample,
                    long long viewport_end_sample,
                    int sample_rate,
                    int last_selected,
                    const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
                    const DragOverlay* drag_overlay = nullptr,
                    cairo_surface_t* ink_plate = nullptr);

// Draws the trim begin/end boundary stems. Each set bound (gated by
// `has_begin` / `has_end`) paints a 1px vertical stem at its domain-frame
// column, spanning the WAVEFORM AREA (top at `waveform_area.y`, down to the
// waveform bottom) — the same extent as marker stems. Trim bounds carry NO
// triangle frame tick (unlike markers): Ableton's loop bounds carry none, so
// the stem+chip pair is the whole waveform-side cue. Color is always
// kTrimMarker — trim has no
// selected variant. `trim.begin` /
// `trim.end` are in the displayed domain (already warp_frame_map-translated by the
// caller), so no further translation happens here — the columns are placed
// exactly like marker stems against the same viewport. View-independent: drawn
// identically in 'W' and 'P' views.
void render_trim_stems(cairo_t* cr,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
                       bool has_begin,
                       bool has_end,
                       cairo_surface_t* ink_plate = nullptr);

// Draws the begin/end trim-boundary chips in the TRIM CHIP LANE (top-strip
// lane 1). Each set bound (gated by `has_begin` / `has_end`) paints a TEXTLESS
// rectangle of the flag's exact width/height (flag_lane_w_px() x
// flag_lane_h_px(), no glyph, no triangle — Ableton's loop bounds carry none),
// centered on its bound column, capping its stem. Chip color is kTrimMarker
// with a kTrimMarkerOutline border. `waveform_area` is the real waveform rect;
// its top edge locates the lanes and its `.w` is the column-mapping
// denominator. Column placement matches render_trim_stems against the same
// viewport — `trim.begin` / `trim.end` are already in the displayed domain, so
// no further translation happens here. The chip has NO editable payload; it is
// a plain-press grab target only (trim is outside the selection system).
// With BOTH bounds set, a wash band fills the trim-chip-lane span between the
// two chips — the visual affordance of the pair (bridge) drag's grab band. It
// occupies the trim-chip lane's vertical band and uses the shared overlay wash
// (kOverlay / kOverlayAlpha, the same pair the phase reset overlay paints
// with), over the strip background. It spans the two bounds' columns
// unconditionally (independent of the chips' viewport cull) clamped into the
// mapped waveform width, so it still paints across the visible part when a chip
// is offscreen.
void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
                       bool has_begin,
                       bool has_end);

// Paints an inert full-width ring around a single strip row's bounding box:
// 1px edges in kOverlay at kOverlayOutlineAlpha, antialias off (the trim
// pair-drag band's ring style, no wash fill). `waveform_width` is the effective
// waveform width (waveform_area.w); the ring spans [row.x, row.x + width) so
// the non-multiple-of-16 right gutter stays outside it. Used by the top
// zoom-strip row (the sole live-drag ring row — the bottom pan row retired).
void render_strip_row_ring(cairo_t* cr, const GuiRect& row, int waveform_width);

// Draws marker flags in `top_strip_area` above visible markers. Each flag is a
// FIXED-WIDTH SHAPE centered on its marker's pixel column (see
// iterate_visible_flags_impl): the flag_lane_w_px() x flag_lane_h_px()
// rectangle in the FLAG LANE plus the tip-down triangle directly beneath it in
// the TRIANGLE LANE, treated as one shape and filled in the marker's color
// class. There is NO TEXT (the payload moves to the marker-text lane in a
// later change). There is no elision — overlapping shapes occlude instead.
//
// Color class:
//   Not selected: fill kMarker, outline kMarkerOutline.
//   Selected:     fill kSelected, outline kSelectedOutline.
// A DISABLED marker's whole shape (rect + triangle + outline) dims under
// kDisabledMarkerAlpha, the same single disabled cue applied to its stem in
// `render_markers`. (Selection owns the COLOR class, disablement owns the
// ALPHA.) Trim membership has no effect on flags.
//
// `warp_frame_map`: same displayed-axis translation as render_markers (the
// live map in target view). Shapes are collected in ascending painted-x order,
// so in target view the occlusion z-order is applied against post-translation
// positions. Painting is two reverse passes keyed on `selected_set` — selected
// shapes above unselected, leftmost on top within each class (see render.cpp).
// `waveform_width` is the EFFECTIVE waveform width (waveform_area.w), the
// column-mapping denominator; flags share the marker stems' samples-per-pixel so
// a flag centered on its column lands over the column its stem rises at, at
// every window width (the width differs from top_strip_area.w only at a
// non-multiple-of-16 window).
void render_flags(cairo_t* cr,
                  GuiRect top_strip_area,
                  int waveform_width,
                  const std::vector<GuiWarpMarker>& markers,
                  long long viewport_start_sample,
                  long long viewport_end_sample,
                  int sample_rate,
                  const std::set<int>& selected_set,
                  const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
                  const DragOverlay* drag_overlay = nullptr);

// Same column placement as render_flags, without drawing — returns the
// screen-coord rects of the flag RECTANGLES that would be rendered (one per
// visible flag; the triangle is not a hit target). One per visible flag, no
// elision, so overlapping shapes yield overlapping rects. The caller
// (hit_test_flag) resolves an overlap with two forward passes mirroring the
// painters' two reverse passes — the leftmost SELECTED containing rect, else
// the leftmost containing rect = the topmost-painted flag. No cairo context is
// needed: the rect is the fixed flag width/height centered on the column.
// `warp_frame_map` mirrors render_flags so the two stay in sync. In target
// view the flags paint at translated positions, so this helper is called with a
// non-null warp_frame_map and the hit-rects walk the same map (see app_state's
// hit-test path). In source view it is null and positions are untranslated.
// `waveform_width` is the effective waveform width (see render_flags): the hit
// rects must use the SAME column-mapping denominator as the paint so the
// clickable rect coincides with the painted flag.
std::vector<FlagHitRect> compute_flag_hit_rects(
    GuiRect top_strip_area,
    int waveform_width,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
    const DragOverlay* drag_overlay = nullptr);

// Phase reset marker analogues. Same pixel layout as the warp
// versions; the visual differences are which list is drawn (phase resets
// instead of warp markers) and the supplied color set. `disabled` is taken
// directly from each phase reset (no label-cascade like warp markers).
// `last_selected` is the active column's anchor index (or -1), the sole marker
// whose stem paints — same contract as render_markers.
void render_phaseresetmarkers(cairo_t* cr,
                              GuiRect waveform_area,
                              const std::vector<GuiPhaseResetMarker>& phase_resets,
                              long long viewport_start_sample,
                              long long viewport_end_sample,
                              int sample_rate,
                              int last_selected,
                              const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
                              const DragOverlay* drag_overlay = nullptr,
                              cairo_surface_t* ink_plate = nullptr);

// The phase-reset flag is the same fixed shape as a warp flag (rectangle +
// triangle centered on the column), textless. Two states only: default fill
// `kMarker`, selected fill `kSelected`; a disabled phase reset dims the whole
// shape under kDisabledMarkerAlpha. Trim membership has no effect.
// `waveform_width` is the effective waveform width (see render_flags), the
// column-mapping denominator shared with the phase-reset stems. Painting is two
// reverse passes keyed on `selected_set` — selected shapes above unselected,
// leftmost on top within each class (see render.cpp).
void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            int waveform_width,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            int sample_rate,
                            const std::set<int>& selected_set,
                            const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
                            const DragOverlay* drag_overlay = nullptr);

// `waveform_width` is the effective waveform width (see compute_flag_hit_rects).
std::vector<FlagHitRect> compute_phase_reset_flag_hit_rects(
    GuiRect top_strip_area,
    int waveform_width,
    const std::vector<GuiPhaseResetMarker>& phase_resets,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr,
    const DragOverlay* drag_overlay = nullptr);

// Iteration-aware flag text composer. Returns the
// plain flag text when `iteration_on` is false or the marker is iter-
// ineligible; otherwise splices the inline `+[lo, hi]` bracket after
// the tempo. The single canonical composer for warp flag text — used to seed
// the flag editor in iteration mode (the flags themselves are now textless
// shapes; the payload text moves to the marker-text lane in a later change).
std::string flag_text_iter(const std::vector<GuiWarpMarker>& markers,
                           int idx, bool iteration_on);

// Per-character pixel advance for the monospace font at flag_font_size_px().
// Measured via init_monospace_grid_metrics(); returns 0 if not yet
// measured. Used by click-to-position-cursor in the
// editor (input_handler.cpp -> flag_editor.cpp).
double monospace_advance();

// Fixed-pixel row height for the strip grid, measured from cairo_font_extents
// (ascent + descent) at flag_font_size_px() plus 2*flag_pad_y_px() plus
// 2*kChipOutlinePx (the chip height IS the row metric, so the outline ring is
// baked in here). Returns kRowHFallbackPx until init_monospace_grid_metrics
// has measured the real font. The vertical twin of monospace_advance();
// consumed by the strip/row geometry helpers (which have no cairo context of
// their own).
int monospace_row_h();

// Baseline offset from a row rect's top edge: flag_pad_y_px() + kChipOutlinePx
// + font ascent. baseline_y = row.y + monospace_row_baseline_offset() centers
// the text in the row the same way the flag chip sits in the top strip.
double monospace_row_baseline_offset();

// Measure and cache the advance width and row metrics. Runs at the top of
// every redraw; no-ops while the pixel size it last measured equals the
// current flag_font_size_px(), and re-measures on the first frame after a
// font_size change. The supplied cairo_t* is used only for measurement;
// the font state is restored on return.
void init_monospace_grid_metrics(cairo_t* cr);

// Returns the on-screen x reference for the given marker's flag-editor caret,
// in pixels: the marker's pixel column plus the flag_glyph_inset_px() glyph
// inset. The flag-editor text does not render in the strip in this design (it
// moves to the marker-text lane later), so this is the caret click-to-byte
// reference only. Returns -1.0 if the marker is not currently visible in the
// viewport. Direct computation -- does not require a cairo context.
double flag_pending_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx);
