#pragma once
#include "warpmarkers.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment for target-view waveform

#include <cairo/cairo.h>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
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
// — accepted, it stays visible. Painted in kPlayheadCursorLight (the playhead
// green lightened toward white) at this alpha, so the region reads as a lighter
// tint of the same green the split playhead marks its bounds in. Architect-
// tunable on the labwc pass; start subtle.
inline constexpr double    kRegionWashAlpha = 0.10;

inline constexpr GuiColor kMarker           = hex(0x9145AD);
inline constexpr GuiColor kSelected         = hex(0x3DAEE9);  // Breeze blue
inline constexpr GuiColor kPlayheadScanner  = hex(0xF2D959);
inline constexpr GuiColor kPlayheadCursor   = hex(0x1ABC9C);  // green cursor
// kPlayheadCursor blended ~55% toward white, per channel c' = round(c + 0.55*(255-c)):
// 0x1A->0x98, 0xBC->0xE1, 0x9C->0xD2. A lighter tint of the playhead green,
// used for the region-select wash (kRegionWashAlpha) so a live region reads as a
// translucent brightening of the same green the split playhead marks its bounds
// in — the wash and the split half-triangles share one hue.
inline constexpr GuiColor kPlayheadCursorLight = hex(0x98E1D2);
// Selected-marker focus grey (architect 2026-07-23). When the marker selection
// is NON-EMPTY a GREY, STEMLESS triangle paints ON each selected marker
// (paint_selected_marker_triangles, over the flag), the focus cue attached to
// the selection itself; NOTHING paints at the resting cursor while a selection is
// live (retiring the earlier R6 grey-triangle-at-the-cursor form). Distinct from
// the waveform-focus breeze green (kPlayheadCursor #1abc9c) and readable against
// the green family. Also distinct in role and value from the strip-drag anchor
// stem grey (kStripAnchorStem #8c8c8c): this is a slightly lighter, faintly
// green-tinted mid grey so the triangle reads as a muted focus cue, not the
// neutral pivot affordance.
inline constexpr GuiColor kPlayheadCursorFocusGrey = hex(0x9AA5A0);
inline constexpr GuiColor kAccent           = hex(0xBF332E);
inline constexpr GuiColor kText             = hex(0xFCFCFC);  // Breeze paper white

// The strip-drag anchor stem's grey — plainly dimmer than kText (#fcfcfc) and
// clearly brighter than kBackground (#1a1a1f). The anchor stem is a transient
// pivot affordance shown only mid-drag, so it reads as a muted guide rather than
// competing with the crisp white marker/text ink. Dimmed by hue, not alpha; the
// kBackground ink-notch overdraw where it crosses waveform samples is unchanged.
inline constexpr GuiColor kStripAnchorStem  = hex(0x8C8C8C);

// The chip outline palette — a brighter sibling of each fill
// (kMarker / kSelected / kAccent / kTrimMarker). Painted as the solid 1px
// outline ring around a chip (see EditorTextBox::outline / kChipOutlinePx);
// these are the tuning knobs.
inline constexpr GuiColor kMarkerOutline    = hex(0xC178E0);
inline constexpr GuiColor kSelectedOutline  = hex(0x7DCDF7);
inline constexpr GuiColor kAccentOutline    = hex(0xE5655F);
inline constexpr GuiColor kTrimMarkerOutline = hex(0xFFA040);

// Single dimming factor for a DISABLED marker, applied uniformly across its
// whole unified textless shape — the rectangle fill, its single outside-only
// outline ring, and the fused tip-down triangle (all drawn by paint_flag_shape).
// This alpha is the disabled cue on the FLAG; selection controls the color class
// (kSelected family) and disablement this alpha, and the two compose. (The stem
// no longer takes this alpha: it became the selected-marker live overlay
// paint_selected_stem — a positional cue that deliberately does not dim for a
// disabled marker, the dimmed flag already conveying disablement.)
// Architect-tunable.
inline constexpr double kDisabledMarkerAlpha = 0.25;

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
// derived surface (strip heights, baseline solves, hit rects) tracks it
// automatically.
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
    int w = static_cast<int>(std::nearbyint(
        static_cast<double>(kFlagWidthPx) * gui_font_scale()));
    // Force the scaled width ODD (bump an even result up by one). An odd flag
    // width is the invariant that keeps the fused tip-down triangle's tip
    // centered exactly on the marker's 1px column AND keeps the playhead mask
    // width (2*playhead_triangle_h_px() - 1) equal to the flag width at every
    // font size: with an even width, playhead_triangle_h_px() = (w+1)/2 rounds
    // down and the mask comes out one pixel NARROWER than the flag (e.g. w=20
    // -> H=10 -> mask 19), breaking the marker/playhead shape identity. The
    // floor below stays odd (5).
    if ((w & 1) == 0) ++w;
    return w < 5 ? 5 : w;
}
inline int flag_lane_h_px() {
    const int h = static_cast<int>(std::nearbyint(
        static_cast<double>(kFlagHeightPx) * gui_font_scale()));
    return h < 5 ? 5 : h;
}

// Height H (px) of the code-generated tip-down triangle, SHARED by the playhead
// cursor and every marker/trim flag. The triangle width is 2*H - 1 (odd by
// construction, so its tip centers exactly on the 1 px column). H is DERIVED
// from the flag width so the widest triangle row is the flag rectangle's OWN
// width: H = (kFlagWidthPx+1)/2 = 9 at scale 1 gives a top row of 2*9-1 = 17 =
// kFlagWidthPx (the odd-row rule — every triangle row is odd, top row 17 down to
// a 1 px tip). The slopes therefore leave the rectangle's exact bottom corners
// and run continuously to the tip with NO inward step — geometrically this is
// the former 15-wide top with its two 1-px corner insets replaced by chamfers
// collinear with the slopes, so the overall marker width is unchanged and the
// rect->triangle outline flows without a 90-degree jog. Clamped to at least 2 so
// the triangle always has a tip row below a top row. The half-width below
// derives from H, so the two can never drift.
inline int playhead_triangle_h_px() {
    const int h = (flag_lane_w_px() + 1) / 2;
    return h < 2 ? 2 : h;
}

// Half-width (px, measured from the triangle's vertical centerline) of the
// shared tip-down flag/playhead triangle at `rows_below_base` pixel rows below
// its BASE (top) edge. The base row spans the full flag width — half-width
// flag_lane_w_px()/2 — and the triangle tapers LINEARLY to a zero-width tip
// playhead_triangle_h_px() rows further down. This is the single owner of that
// taper: paint_flag_shape derives the triangle's base corners and apex from it,
// and hit_test_flag uses it to decide whether a point in the triangle lane is
// inside the shape, so the painted slope and the clickable slope cannot drift.
// Clamped to the [base, tip] span (0 above the base, 0 at/below the tip).
inline double flag_triangle_half_width_at(double rows_below_base) {
    const double H = static_cast<double>(playhead_triangle_h_px());
    if (H <= 0.0) return 0.0;
    double t = rows_below_base / H;   // 0 at the base, 1 at the tip
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return (static_cast<double>(flag_lane_w_px()) / 2.0) * (1.0 - t);
}

// The cached cairo A8 mask surface for the tip-down triangle (W = 2H-1 by H,
// ANTIALIASED — the triangle path is filled with AA enabled so the two slopes
// carry baked gray edge alphas). Owned by render.cpp file-scope state beside the
// grid metrics; regenerated when H changes. Stamps the PLAYHEAD cursor triangle
// (centered on the column, tip at the waveform top edge); the per-frame playhead
// redraws take the cheap cached-mask stamp rather than a live path fill. The
// marker/trim flags fill their own AA triangle path in paint_flag_shape (so fill
// and outline blend as one shape); both are the identical 17-wide/H=9 geometry.
// Never null.
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
// render.cpp (cull) and main.cpp (invalidation). At scale 1 it is 8.
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

// Char-0 origin (px) of an editor's editable text run: the box anchor plus the
// static prefix's exact monospace advance (glyph count * monospace_advance()).
// The ONE owner shared by render_editor_text_box's paint-time editable_left and
// the click-to-caret geometry (active_editor_text), so the caret origin can
// never drift from the painted glyph run. std::string_view accepts both the
// const char* editor prefixes and render_editor_text_box's std::string prefix.
inline double editor_text_glyph0_x(double anchor_x, std::string_view prefix) {
    return anchor_x +
        static_cast<double>(prefix.size()) * monospace_advance();
}

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
// Inputs (sole caller today: render_editor_text_box, for the marker-text-lane
// flag editor and the bottom-strip editors):
//   text_left   - the box's left edge as the caller positioned it (back-derived
//                 from the editable-text anchor via EditorTextBox::hl_pad).
//                 Chip left edge = round(text_left); the glyph run sits
//                 flag_glyph_inset_px() (ring + left inner pad) to the right of
//                 it, folded into where the caller places the glyph origin vs.
//                 text_left (see consumers).
//   glyph_count - number of glyphs in the box's text (== text.length()).
//                 Width = flag_chip_width_px(glyph_count).
//   baseline_y  - the text baseline y the caller solved for its row. The box
//                 top is baseline_y - monospace_row_baseline_offset(); the
//                 height is the full monospace_row_h() slot.
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
// vertical line is drawn. `draw_line = false` is the complementary suppression:
// the triangle draws but the 1px vertical line does not — the grey, STEMLESS
// selected-marker FOCUS triangle (paint_selected_marker_triangles, painted ON
// each selected marker, architect 2026-07-23). The two flags are independent:
// the scanner is line-only (triangle off, line on), the waveform-focus cursor is
// both on, and the selected-marker focus triangle is triangle-only (line off).
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
                     bool draw_line = true,
                     cairo_surface_t* ink_plate = nullptr);

// Draws the SPLIT playhead shown while a region-select is active: the normal
// single cursor triangle dissolves and TWO half-triangles take its place, one on
// each region bound. Both halves stamp the ONE cached playhead triangle mask
// (playhead_triangle_mask(), 2H-1 wide, tip-down, full-height center column at
// image index H-1) clipped to a half each — no new masks are built. left_col /
// right_col are the region's bound columns relative to `area.x` (screen column =
// area.x + col; left = the smaller, right = the larger). The LEFT half keeps mask
// columns [0..center], blitted so the center column lands ON left_col — the
// full-height edge sits on the bound and the slope flares LEFT, outside the
// region. The RIGHT half keeps mask columns [center..2*center], center column on
// right_col, slope flaring RIGHT. Same triangle lane and dst_y as the unsplit
// playhead triangle, and each half is additionally clipped to the waveform's
// horizontal span so a bound near an edge partial-renders instead of leaking.
// left_col == right_col is handled as a special case that stamps the full mask
// ONCE, yielding exactly the ordinary single cursor triangle (stamping the two
// halves would double-composite the shared AA tip column under OVER).
void render_split_playhead(cairo_t* cr,
                           GuiRect area,
                           int left_col,
                           int right_col,
                           GuiColor color);

// Draws the strip-drag ANCHOR STEM: a 1-pixel vertical line at the drag's pivot
// column `col` (window pixels within `area`, clamped here to [0, area.w-1]),
// spanning the full waveform height like a marker stem, in the dimmer-grey
// kStripAnchorStem (a transient drag affordance, deliberately less loud than a
// marker stem). The anchor is
// the clamped column the strip-drag math pins each event — edge-included, so an
// edge-pinned anchor draws the stem exactly at the edge and the clamp becomes
// visible (the Ableton affordance). Over waveform ink the same kBackground notch
// overdraw the marker stems use (fill_column_ink_runs against `ink_plate`, the
// displayed plate) recolors the crossing so the stem reads as a dark cut. The
// vertical line is hard-aliased at the +0.5 half-pixel column. `ink_plate` may
// be null (no notch).
void render_strip_anchor_stem(cairo_t* cr,
                              GuiRect area,
                              int col,
                              cairo_surface_t* ink_plate = nullptr);

// (The cached marker-stem renderers render_markers / render_phaseresetmarkers
// are retired: the marker stem became a live overlay,
// GuiPaintHandler::paint_selected_stem — the SINGLE selected marker's stem, shown
// while it is hovered / dragged / nudged — round 3, refined round 4, architect
// 2026-07-23. The stem cache now carries only the trim stems below.)

// The ONE trim bound-to-column geometry owner (audit C1). Every consumer of a
// trim bound's pixel column funnels here: the two paint sites (render_trim_stems'
// waveform stem, render_trim_flags' chips / strip stems / wash gap) and the two
// hit sites (hit_test_trim_chip's chip rects, route_trim_chip_press' bridge
// test). It replaced five hand-copied `nearbyint` + `clamp(0, W-1)` formulas
// maintained "byte-identical" by comment discipline.
//
// PURE: all basis inputs are parameters — the collapse unifies the FORMULA, NOT
// the basis choice. The basis stays per-caller BY RULING (the event-synchronized
// hit-geometry doctrine): the painters call with the fp-recipe viewport they are
// handed from the caches (and `displayed_ms` already mapped by
// compute_displayed_trim); the hit sites call with the LIVE viewport, their
// derived `vp_end` (= vp_start + nearbyint(spp*wave_w)), and `displayed_ms`
// mapped through displayed_or_live_target_map by displayed_trim_ms. Do not
// "helpfully" unify the basis.
//
// The x_raw denominator is the PAINTERS' quantized-span form
// (vp_end - vp_start)/wave_w, NOT current_samples_per_pixel. The two are
// identical at integer zoom rungs on multiple-of-16 widths and differ by
// <~0.02 px at a fractional zoom rest; adopting it at the hit sites too (they
// formerly divided by spp) is the one deliberate byte change of the collapse and
// ALIGNS paint and hit exactly — the point of unifying them.
//
// EOF-WALL CLAMP (the one copy, formerly installed at three sites at once):
// `col` clamps col_raw into the visible column range [0, wave_w-1]. The
// inclusive END wall T-1 at full zoom-out rounds to column wave_w (one past the
// surface); left unclamped, the right-edge-anchored end chip loses its
// bound-edge pixel and outline to the cache clip and its stems fall offscreen.
// Clamping lands the wall on the last visible column so the chip stays fully
// visible and connected. Begin/frame-0 already maps to column 0, unaffected.
// The wash band deliberately reads col_raw (unclamped) for an OFFSCREEN bound so
// its fill/ring follow the chip past the viewport edge (clipped by the surface).
struct TrimBoundColumn {
    double ms;          // displayed-domain position (already mapped)
    bool   in_viewport; // ms in [vp_start, vp_end)
    int    col_raw;     // unclamped nearbyint column
    int    col;         // clamped into [0, wave_w-1] (the EOF-wall clamp)
};
TrimBoundColumn trim_bound_column(double displayed_ms,
                                  long long vp_start, long long vp_end,
                                  int wave_w);

// The source-frame -> displayed-domain mapping the two HIT sites share
// (add_chip, bound_col). Byte-identical to render.cpp's file-local
// frame_to_paint_sample for every reachable (non-negative) trim bound: in a
// mapped view the source frame is rounded once through map_source_to_target,
// then rounded again; the identity (null/empty map) path returns the frame
// as-is. A negative frame is guarded to 0 (unreachable — past-EOF is load-fatal
// and bounds are never negative — kept for exactness vs the prior hit code).
// The PAINT sites do NOT call this: they receive pre-mapped `displayed_ms` from
// compute_displayed_trim (their fp-recipe basis), which maps once for both
// caches. `map` is null in source view (identity) and the item pixels' own map
// (displayed_or_live_target_map) in target view.
double displayed_trim_ms(int64_t frame,
                         const std::vector<WarpFrameMapSegment>* map);

// The ONE trim chip screen-rect owner (audit C1): the begin/end edge-anchoring
// rule lives here, consumed by both the painter (render_trim_flags) and the hit
// test (hit_test_trim_chip). A trim bound is an EDGE, not a point: the begin
// chip's LEFT edge sits ON the bound column (rect left = strip_x+col), the end
// chip's RIGHT edge sits on it (rightmost pixel = strip_x+col, so rect left =
// strip_x+col - flag_w + 1). The chip is flag-sized (flag_lane_w_px() wide); its
// y-band comes from `row` (the trim chip lane = top_upper_row_area). Deliberate
// asymmetry vs centered marker flags: a bound at frame 0 / EOF shows its chip
// fully onscreen.
GuiRect trim_chip_rect(bool is_begin, int strip_x, int col, GuiRect row);

// Draws the WAVEFORM-AREA portion of the trim begin/end boundary stems. Each set
// bound (gated by `has_begin` / `has_end`) paints a 1px vertical stem at its
// domain-frame column, spanning the WAVEFORM AREA (top at `waveform_area.y`, down
// to the waveform bottom). Unlike marker stems (waveform-only), the trim stem
// ALSO has a strip-crossing segment above it — from the chip's bottom edge down
// through the intervening lanes to the waveform top — painted by render_trim_flags
// (top-strip pass); the two segments meet at `waveform_area.y` to form one
// unbroken line at the bound column. Trim bounds carry NO
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
// lane 1), plus the strip-crossing portion of their stems and the inter-chip
// bridge band. Each set bound (gated by `has_begin` / `has_end`) paints a
// TEXTLESS rectangle of the flag's exact width/height (flag_lane_w_px() x
// flag_lane_h_px(), no glyph, no triangle — Ableton's loop bounds carry none),
// EDGE-ANCHORED on its bound column: the begin chip's LEFT edge on the column
// (body rightward), the end chip's RIGHT edge on it (body leftward). A bound is
// an EDGE, not a point — the deliberate asymmetry vs centered marker flags — so
// a bound at frame 0 / EOF shows its chip fully onscreen. Chip color is
// kTrimMarker with a kTrimMarkerOutline border. `waveform_area` is the real
// waveform rect; its top edge locates the lanes and its `.w` is the
// column-mapping denominator. Column placement matches render_trim_stems against
// the same viewport — `trim.begin` / `trim.end` are already in the displayed
// domain, so no further translation happens here. The chip has NO editable
// payload; it is a plain-press grab target only (trim is outside the selection
// system). Below each chip, a 1px stem segment runs from the chip's bottom edge
// down to the waveform top, meeting the render_trim_stems waveform segment there
// as one unbroken line at the bound column.
// With BOTH bounds set, a wash band fills the GAP between the two edge-anchored
// chips (begin chip's inner edge to end chip's inner edge) — the visual
// affordance of the pair (bridge) drag's grab band. It occupies the trim-chip
// lane's vertical band and uses the shared overlay wash (kOverlay /
// kOverlayAlpha, the same pair the phase reset overlay paints with) with a 1px
// ring around it, over the strip background. Columns are computed unconditionally
// (independent of the chips' viewport cull) and clamped into the mapped waveform
// width, so it still paints across the visible part when a chip is offscreen; a
// gap shows only when the span is wide enough that the chips do not overlap.
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
// class. There is NO TEXT (the payload lives in the marker-text lane, shown on
// hover and edited in the Enter flag editor). There is no elision — overlapping
// shapes occlude instead.
//
// Color class (`red_set` = the store indices whose render normalizes to the
// 1.00 fallback, from warp_red_flag_set_cached):
//   Selected:          fill kSelected, outline kSelectedOutline (wins).
//   Red (in red_set):  fill kAccent,   outline kAccentOutline.
//   Otherwise:         fill kMarker,   outline kMarkerOutline.
// A DISABLED marker's whole shape (rect + triangle + outline) dims under
// kDisabledMarkerAlpha, applied on top of whichever color class. (Selection wins
// the COLOR class over red, disablement owns the ALPHA.) Trim membership has no
// effect on flags.
//
// `warp_frame_map`: the displayed-axis translation the painters share (the
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
                  const std::set<int>& red_set,
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

// The phase-reset flag is the same fixed shape as a warp flag (rectangle +
// triangle centered on the column), textless. Color class: selected fill
// `kSelected` (wins), else red (in `red_set` — a coincident-collapse member
// from phase_reset_red_flag_set_cached) fill `kAccent` with `kAccentOutline`,
// else default fill `kMarker`; a disabled phase reset dims the whole shape
// under kDisabledMarkerAlpha on top. Trim membership has no effect.
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
                            const std::set<int>& red_set,
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

// Left x (window pixels) of a transient text run of `glyph_count` monospace
// glyphs shown in the marker-text lane over marker `marker_idx`'s painted
// column. Both lane occupants — the hover popup and the flag editor — center
// their run on the marker and clamp it fully onscreen within the lane (unlike
// the flags, the lane text never hangs off an edge); this is that one
// placement owner. Uses the painters' own column math against
// displayed_or_live_target_map — the event-synchronized displayed basis the
// flag pixels were painted with (identity/empty in source view; in target view
// the map the last committed frame's flag cache baked, with the live map as the
// cold-state fallback) — so the run centers on the same column the flag paints.
// Returns -1.0 for an invalid marker index; a valid off-view marker still
// returns a clamped onscreen origin (the lane text is always visible). No cairo
// context.
double lane_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx, size_t glyph_count);

// The frame-addressed core of lane_text_left_x: same placement math, but keyed
// on a marker's authored source frame rather than a warp-store index, so the
// phase-reset column's lane hover (which lives in a different store) shares one
// placement owner with the warp column. The idx overload above delegates here.
double lane_text_left_x_at_frame(
    const AppState& app, const GuiAudio& audio,
    double source_frame, size_t glyph_count);

// The flag editor's caret / text-run origin owner: lane_text_left_x sized by
// the flag editor's current pending text. The single reference the lane paint,
// the click->byte caret math, and the editor-text drag all read, so what is
// shown is where the caret lands. Returns -1.0 for an invalid marker index. No
// cairo context.
double flag_pending_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx);

// The marker-text lane's current NON-EDITOR run, arbitrated once so the paint
// pass and the unified marker hit resolver (marker_hit_at, below — the run is
// the marker's second hittable part beside its flag shape) read
// the SAME run and cannot drift.
// Mirrors the precedence paint_marker_text_lane owns below the flag-editor
// case: tier 1 the HOVERED marker's own value (hover_popup.lane_text at
// hover_popup.source_frame), else tier 2 the LAST-SELECTED marker's own value
// composed from the live store — flag_text_iter for a warp marker, the "p"
// literal for a phase reset — with the mid-drag proposed-position substitution
// (a DragOverlay membership lookup: the dragged member's live moveable time,
// covering group drags) and the painted-column offscreen cull the flags apply.
// `valid` is false when
// no run shows. `marker_index` is the active-column store index (warp or
// phase-reset per active_markers_view); `source_frame` is the DOUBLE centering
// basis (the mid-drag substituted position included); `text` is the composed
// run. The FlagPayload flag-editor case is NOT covered here — it owns the lane
// alone and is resolved ahead of this call at both consumers.
struct LaneTextRun {
    bool        valid        = false;
    int         marker_index = -1;
    double      source_frame = 0.0;
    std::string text;
};

// Resolve the current marker-text-lane run (the non-editor arbitration above),
// the single owner both paint_marker_text_lane and the unified marker hit
// resolver read so the painted run and the clickable run are one run. The
// run's screen rect is derived by the caller exactly as paint does (left =
// lane_text_left_x_at_frame(app, audio, source_frame, text.size()), a left<0
// meaning the advance is not yet measured). No cairo context.
LaneTextRun current_marker_lane_run(const AppState& app, const GuiAudio& audio);

// The unified marker hit: the marker is ONE pointer item, hit either by its
// FLAG SHAPE (hit_test_flag: the fixed rectangle plus the fused triangle,
// topmost-painted wins) or by its RENDERED MARKER-TEXT LANE RUN (the run
// current_marker_lane_run resolves — the ONE run arbitration the lane paint
// also reads — when the point lands inside the run's screen rect, derived
// exactly as paint derives it: lane_text_left_x_at_frame for the left edge,
// glyph count times monospace_advance for the width, top_marker_text_row_area
// for the y-band). `on_flag` records WHICH part was hit for the one asymmetry
// the parts keep: the flag is the sole DRAG handle (a run press selects /
// double-clicks / lands but never arms a reposition; the hover recompute reads
// only .index, the surface not mattering to hover). index is the
// active-column store index, -1 when neither part is under the point. Pure
// geometry over app/audio (no cairo context); homed here beside
// current_marker_lane_run so the press chain (input_pointer.cpp) and the hover
// recompute (viewport.cpp) share one resolver.
struct MarkerHit {
    int  index   = -1;
    bool on_flag = false;
};

MarkerHit marker_hit_at(const AppState& app, const GuiAudio& audio,
                        int x, int y);
