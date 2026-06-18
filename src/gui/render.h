#pragma once
#include "warpmarkers.h"
#include "phase_reset_markers.h"
#include "frame_map.h"   // FrameMapSegment for target-view waveform

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
// target-frame in target view). Trim no longer dims any
// renderer — it is consumed only by render_trim_stems to place the two
// boundary stems. Values match the convention in compute_trim_samples
// (main.cpp).
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
inline constexpr GuiColor kMarker           = hex(0x9145AD);
inline constexpr GuiColor kSelected         = hex(0x3DAEE9);  // Breeze blue
inline constexpr GuiColor kPlayheadScanner  = hex(0xF2D959);
inline constexpr GuiColor kPlayheadCursor   = hex(0x1ABC9C);  // green cursor
inline constexpr GuiColor kAccent           = hex(0xBF332E);
inline constexpr GuiColor kText             = hex(0xFCFCFC);  // Breeze paper white

// Trim boundary stem color (#F67400 orange). Distinct from
// kMarker, kSelected, the teal cursor, and the yellow scanner. A set
// trim begin/end paints as a vertical stem in this color, or kSelected
// when that boundary is selected.
inline constexpr GuiColor kTrimMarker       = hex(0xF67400);  // Breeze orange

// Flag chip internal padding around the text glyph bounding box, split per
// axis so the two can be tuned independently. Both are the single source of
// truth for their axis — every chip renderer and the hit-rect computation must
// read these, never a literal.
//
// kFlagPadXPx sets chip WIDTH: the painted fill and the hit rect both span
// glyph_advance + 2*kFlagPadXPx, and the elision pack uses it as the inter-chip
// touch threshold.
//
// kFlagPadYPx sets chip HEIGHT via the row metric: the row height is
// font (ascent+descent) + 2*kFlagPadYPx, and the baseline offset is
// kFlagPadYPx + ascent. Lowered from an effective 4 to 2 here; the old
// vertical-only kVPadExtraPx remnant (outline-era stroke clearance, obsolete
// since the migration to solid fills) is gone.
constexpr double kFlagPadXPx = 4.0;
constexpr double kFlagPadYPx = 2.0;

// Sole authored value for the flag chip's vertical anchor: the offset from
// the waveform area's top edge up to the regular flag rect's painted bottom
// edge. Now 0 — the chip bottom is flush with the waveform area's top edge
// (flag_chip_bottom_y(area, Lower) returns area.y exactly), so the regular
// marker stem begins at the chip bottom = area top and runs straight down
// with no overhang above the area. The constant survives as the lower-row
// term of the stem-cache overhang (via kStemAboveWaveformPx, now 0) and as
// the flag_chip_bottom_y Lower anchor (now identity); it is still consumed by
// render_flags' baseline computation, the stem renderers, and the iter/BPM
// popups in main.cpp that mirror the flag rect's vertical position. The
// waveform-internal inset for the cursor triangle is a SEPARATE constant
// introduced in geom-cursor, not a rename of this one.
constexpr double kFlagBottomLiftPx = 0.0;

// The lower-row component of the stem-cache overhang: the distance from the
// waveform top up to the regular (lower-row) flag chip's bottom edge. Now 0
// (defined off kFlagBottomLiftPx, which is 0): the lower-row stem no longer
// overhangs above the waveform top — it begins at the chip bottom = area top.
// The full stem-cache overhang is still the TALLER trim value (see
// stem_cache_overhang_px, which adds one row + gap on top of this), since
// trim stems reach up to the upper-row chip bottom and share the same cache
// surface. Defined by derivation off kFlagBottomLiftPx so the lower-row
// component tracks the one authored lift value.
constexpr double kStemAboveWaveformPx = kFlagBottomLiftPx;

// Fixed-pixel mirrored four-row strip grid. G is the single tunable
// inter-row gap, shared between the two rows of each strip; it doubles as the
// trim-flag-to-regular-flag distance. One named constant,
// one place to change it. Now 0 — the two rows of each strip touch, and the
// waveform-side and outer (window-edge) gaps (both kFlagBottomLiftPx, also 0)
// vanish, so rows and strips pack tight against each other and the window
// edges.
constexpr double kRowGapPx = 0.0;

// Defensive window floor (a conservative 640x480 minimum). Enforced two ways:
// the Wayland set_min_size hint at toplevel creation, and an internal clamp in
// the geometry helpers so the waveform arithmetic is always valid regardless of
// what the compositor sends. Not sized to fit content — the longest dialogue
// may clip at the floor, which is acceptable (nobody authors at 640x480).
constexpr int kMinWindowWidthPx  = 640;
constexpr int kMinWindowHeightPx = 480;

// Waveform-internal top/bottom inset, in pixels. The drawn waveform samples
// are confined to [area.y + kWaveformInsetPx, area.y + area.h - kWaveformInsetPx]
// so the cursor triangle has a clear band to sit in at the top, and the
// waveform is symmetric about its area center. MUST equal the height of
// assets/playhead-cursor.png (19x10) — the triangle exactly fills the top band,
// tip at the first sample row. If the asset height changes, change this to
// match (and kPlayheadHalfPx for the width; see render.cpp). This is NOT the
// old strip gap (kFlagBottomLiftPx, now 0) revived — it is a distinct
// waveform-internal margin that happens to share the value 10 because that is
// the triangle's height.
constexpr int kWaveformInsetPx = 10;

// Half-width (px) of the playhead triangle's horizontal footprint; bounds
// the playhead's off-screen cull and its invalidation strip. Single
// definition shared by render.cpp (cull) and main.cpp (invalidation), both of
// which formerly held their own anonymous-namespace copy of the value.
inline constexpr int kPlayheadHalfPx = 9;

// Pre-first-paint fallback for the measured monospace row height and baseline
// offset (Liberation Mono 11pt). on_resize can fire before the first redraw
// measures the real font; these seed the geometry so it is sane (never a
// negative waveform) until init_monospace_grid_metrics overwrites them.
constexpr int    kRowHFallbackPx       = 22;
constexpr double kRowBaselineOffFallbackPx =
    kFlagPadYPx + 14.0;

// Forward declaration: defined with its full doc comment below. Needed here
// because flag_chip_bottom_y / stem_cache_overhang_px (inlines) read it.
int monospace_row_h();
// Forward declaration: defined with its full doc comment below. Needed here
// because flag_chip_rect (inline) reads it.
double monospace_row_baseline_offset();
// Forward declaration: defined with its full doc comment below. Needed here
// because flag_chip_rect (inline) computes the chip width from it.
double monospace_advance();

// Which strip row a chip's bottom edge sits at. Lower is the regular
// warp/phase-reset flag row (now flush with the waveform area top, since
// kFlagBottomLiftPx is 0); Upper is the begin/end trim-flag row, one row + one
// inter-row gap higher.
enum class ChipRow { Lower, Upper };

// The flag chip's painted bottom edge for the current layout. With
// kFlagBottomLiftPx now 0, the regular (Lower) chip's bottom edge is flush
// with the waveform area top. This is the single source of
// truth that both the flag baseline solve and the stem renderers
// (render_marker_stems_impl, render_trim_stems, render_trim_flags) read: the
// stem is an extension of the flag, so its top originates here and runs down
// to the waveform bottom. Length is whatever connects the chip bottom to the
// waveform bottom.
//
// `row` selects which strip row the chip caps. Lower returns the waveform
// area top exactly (regular flags/stems are flush, kFlagBottomLiftPx == 0).
// Upper sits one row + one inter-row gap higher — the trim begin/end flags,
// whose stems are therefore longer by that amount automatically.
inline double flag_chip_bottom_y(const GuiRect& waveform_area, ChipRow row) {
    const double lower =
        static_cast<double>(waveform_area.y) - kFlagBottomLiftPx;
    if (row == ChipRow::Lower) return lower;
    return lower - (static_cast<double>(monospace_row_h()) + kRowGapPx);
}

// Single source of truth for a flag chip's painted/hit rectangle. ALL chip
// types — regular warp flags, phase-reset flags, and trim b/e chips — derive
// their fill rect AND their hit rect from this one function, so the two cannot
// drift.
//
// Width is computed from the cached per-character monospace advance
// (monospace_advance(), measured once on the real paint surface at startup),
// NOT from a per-call cairo_text_extents — that was the residual edge bug:
// paint measured on the window surface, hit on a 1x1 scratch surface, and the
// integer width diverged by 1px. Chip text is ASCII-only, so glyph_count is the
// exact glyph count and glyph_count * monospace_advance() is the exact advance,
// identical at paint and hit, with zero surface dependence.
//
// Inputs:
//   text_left   - glyph paint x, already snapped to the marker's integer pixel
//                 column by the caller. Chip left edge = round(text_left); the
//                 kFlagPadXPx left inner pad is folded into where the caller
//                 places text_left vs. the glyph origin (see consumers).
//   glyph_count - number of glyphs in the chip's text (== text.length() for the
//                 ASCII chip strings). Width = round(count*advance + 2*pad).
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
    const double pad = kFlagPadXPx;
    const double advance =
        static_cast<double>(glyph_count) * monospace_advance();
    GuiRect r;
    r.x = static_cast<int>(std::round(text_left));
    r.y = static_cast<int>(std::round(
              baseline_y - monospace_row_baseline_offset()));
    r.w = static_cast<int>(std::round(advance + 2.0 * pad));
    r.h = monospace_row_h();
    return r;
}

// Stem-cache surface overhang above the waveform top, in pixels. Sized for
// the TALLER trim stem (whose top reaches the upper-row chip bottom), so the
// single shared stem-cache surface holds both marker stems (originating at
// the lower-row chip bottom) and trim stems without clipping. Marker stems
// land transparently lower within the same surface; their absolute screen
// position is unchanged. Used by maybe_rebuild_stem_cache (surface height,
// local-area offset) and the on_redraw blit/gate, which must agree.
inline int stem_cache_overhang_px() {
    return static_cast<int>(kStemAboveWaveformPx)
         + monospace_row_h()
         + static_cast<int>(kRowGapPx);
}

// Editor pixel size for the flag-payload editor, iter popup, and BPM
// popup. Computed as 11 pt at 96 DPI (the conventional Linux default
// at non-HiDPI). warptempo_gui does not currently support HiDPI; this is
// a fixed pixel value rather than a runtime pt->px conversion. The
// literal computation form makes the pt origin self-documenting --
// the compiler folds it to a constant at compile time.
constexpr double kFlagFontSize = 11.0 * 96.0 / 72.0;

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
struct EditorTextBox {
    double               anchor_x        = 0.0;
    double               baseline_y      = 0.0;
    std::string          prefix;            // optional; "" = none
    std::string          text;              // editable content
    double               hl_pad           = kFlagPadXPx;
    GuiColor             fill             = kMarker;
    GuiColor             text_color       = kText;
    bool                 has_selection    = false;
    int                  selection_start  = 0;
    int                  selection_end    = 0;
    bool                 cursor_visible   = false;
    int                  cursor_pos       = 0;
};
void render_editor_text_box(cairo_t* cr, const EditorTextBox& s);

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

// Centered single-line status message, shown in place of the (removed) load
// progress bar for the duration of app.loading. Selects the monospace face,
// measures the string via monospace_advance(), centers it in `area`
// horizontally and on `area`'s vertical mid-line, and shows it in kText.
void render_status_message(cairo_t* cr, GuiRect area, const char* msg);

// Draws one channel's waveform into `area`, displaying samples in
// [viewport_start_sample, viewport_end_sample). When `frame_map` is null
// (source view) the viewport range is interpreted in source-frame and
// each column reads `audio.get_peak_range` directly. When `frame_map` is
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
                     const std::vector<FrameMapSegment>* frame_map = nullptr);

// Draws a thin 1px vertical line across `area` at column `playhead_pixel_x`
// (offset from area.x, float for subpixel centering). No-op if outside.
// `triangle_surface` is the pre-loaded playhead-triangle indicator (loaded by
// GuiPlatform); it's stamped above the stem via cairo_mask_surface, tinted with
// `color`. May be nullptr — in that case the indicator is skipped. The
// triangle belongs to the cursor exclusively under the split-playhead
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
                     cairo_surface_t* triangle_surface,
                     bool draw_triangle = true,
                     cairo_surface_t* ink_plate = nullptr);

// Draws vertical 1-pixel lines across `waveform_area` for each marker whose
// resolved sample falls inside [viewport_start_sample, viewport_end_sample).
// Effective disabled state is computed inline from the marker list (a label
// reference inherits the disabled flag of its defining marker). Disabled
// markers are skipped entirely. Selected markers paint kSelected, the rest
// kMarker; marker stems do not dim — only the out-of-trim sample pixels
// dim, via on_redraw's ATOP overlay (see kWaveformDimmed). The global
// out-of-trim dim was retired and it stays retired for stems.
// `frame_map` (default null) shifts marker positioning into the target-frame
// domain when target view is active: each marker's source-frame position is
// run through `map_source_to_target` before viewport clipping and column
// placement. Null frame_map = identity.
void render_markers(cairo_t* cr,
                    GuiRect waveform_area,
                    const std::vector<GuiWarpMarker>& markers,
                    long long viewport_start_sample,
                    long long viewport_end_sample,
                    int sample_rate,
                    const std::set<int>& selected_set,
                    const std::vector<FrameMapSegment>* frame_map = nullptr,
                    const DragOverlay* drag_overlay = nullptr,
                    cairo_surface_t* ink_plate = nullptr);

// Draws the trim begin/end boundary stems. Each set bound
// (gated by `has_begin` / `has_end`) paints a 1px vertical stem at its
// domain-frame column, spanning the same vertical extent as marker stems
// (the flag chip's bottom, via flag_chip_bottom_y, down to waveform bottom).
// Color is
// kTrimMarker, or kSelected when that bound is selected. `trim.begin` /
// `trim.end` are in the displayed domain (already frame_map-translated by
// the caller), so no further translation happens here — the columns are
// placed exactly like marker stems against the same viewport. View-
// independent: drawn identically in 'W' and 'P' views.
void render_trim_stems(cairo_t* cr,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
                       bool has_begin,
                       bool begin_selected,
                       bool has_end,
                       bool end_selected,
                       cairo_surface_t* ink_plate = nullptr);

// Draws the begin/end trim-boundary flag chips in the upper top row.
// Each set bound (gated by `has_begin` / `has_end`) paints a single-glyph
// chip — `b` for begin, `e` for end — capping its stem as one continuous
// unit. Chip color mirrors the stem exactly: kTrimMarker, or kSelected when
// that bound is selected. The chip's bottom edge sits at
// flag_chip_bottom_y(waveform_area, ChipRow::Upper); `waveform_area` is the
// real waveform rect (the chip's vertical anchor is derived from its top
// edge, the same convention the stem renderer uses). Column placement matches
// render_trim_stems against the same viewport — `trim.begin` / `trim.end` are
// already in the displayed domain, so no further translation happens here.
// The chip has NO editable payload; it is select/drag only and is never a
// text_editor target.
void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       double font_size,
                       const TrimRange& trim,
                       bool has_begin,
                       bool begin_selected,
                       bool has_end,
                       bool end_selected);

// Editor overlay used by the top-flag editor. When `marker_index >= 0`
// and matches a flag the renderer is about to draw, that flag's text is
// replaced with `pending` and the background paints either kMarker (normal)
// or kAccent (when `is_red` indicates parse failure). A 1px-wide cursor is
// drawn at the x-position corresponding to `cursor_pos` (byte index into
// `pending`). `cursor_visible` toggles the bar on/off for blink. Pass
// marker_index = -1 to disable the overlay (normal rendering).
struct FlagEditorOverlay {
    int         marker_index        = -1;
    std::string pending;
    int         cursor_pos          = 0;
    // Selection range within `pending`. When
    // `has_selection` is true, the renderer paints a foreground/
    // background swap over [selection_start, selection_end). The
    // cursor continues to paint normally on top, producing the
    // standard inverted-cursor look when the cursor sits inside the
    // selection.
    bool        has_selection       = false;
    int         selection_start     = 0;
    int         selection_end       = 0;
    bool        is_red              = false;
    bool        cursor_visible      = false;
};

// Draws flag annotations in `top_strip_area` above visible markers. Iterates
// left-to-right and greedily skips any flag whose left edge would collide
// with the previously-rendered flag's right edge (+ small pad). Flag text
// is the canonical post-pipe payload: owned tempo `1.28`, owned+scale
// `1.28*1.2345`, owned+def `1.28:a.01`, owned+scale+def `1.28*1.2345:a.01`,
// inherit `pass`, label reference `a.01`.
//
// Three-state model: each flag renders in one of three states.
//   1. Not selected: text in `kText`, no background fill.
//   2. Selected, editor not engaged: background fill in `kMarker`, text
//      in `kText`. No cursor.
//   3. Selected, editor engaged: state 2 plus a 1-px blinking cursor.
// Parse-fail variant of state 2/3: fill is `kAccent` instead of `kMarker`.
// Trim membership has no effect on flags — they always paint
// full-brightness.
//
// Disabled markers render identically to enabled markers in the top strip;
// the only disabled signal lives in the marker stem (handled by
// `render_markers`).
// `frame_map`: same target-frame translation as render_markers. The greedy
// pack and elision still walk left-to-right, so in target view the pack
// rule is applied against post-translation positions (a region the frame_map
// stretches may un-elide flags that were elided in source view; a region
// the frame_map compresses may elide flags that were visible there).
void render_flags(cairo_t* cr,
                  GuiRect top_strip_area,
                  const std::vector<GuiWarpMarker>& markers,
                  long long viewport_start_sample,
                  long long viewport_end_sample,
                  int sample_rate,
                  double font_size,
                  const std::set<int>& selected_set,
                  const FlagEditorOverlay& editor = {},
                  const std::vector<FrameMapSegment>* frame_map = nullptr,
                  const DragOverlay* drag_overlay = nullptr,
                  bool iteration_on = false);

// Paints ONE flag — the FlagPayload-editor target — with the
// live pending text, selection swap, and blinking cursor. Same greedy-
// pack and elision rules as render_flags so the flag lands at the same
// pixel column the cache rendered the other flags at. `editor.marker_
// index` selects which flag emits paint; non-matching emit_indices are
// skipped. Intended caller path: in on_redraw, after the flag-cache
// blit, when overlay.marker_index >= 0 and the active marker-view
// admits FlagPayload editing (not 'P', not render-view). The flag-
// cache itself passes the editor target into the skip-guard inside
// render_flags so the cache leaves a transparent hole over the editor
// target's column; this helper fills the hole with fresh pixels every
// paint, owning the ephemeral cursor blink and pending-text width
// changes without dragging the cache fingerprint.
void render_one_editor_flag(
    cairo_t* cr,
    GuiRect top_strip_area,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::set<int>& selected_set,
    const FlagEditorOverlay& editor,
    const std::vector<FrameMapSegment>* frame_map = nullptr,
    const DragOverlay* drag_overlay = nullptr,
    bool iteration_on = false);

// Same greedy-pack and elision logic as render_flags, without drawing —
// returns the screen-coord rects of the flags that would be rendered. The
// caller uses these for hit-testing. No cairo context is needed: the chip
// width comes from the cached monospace advance (glyph count times
// monospace_advance()), which is exact for the ASCII monospace chip strings.
// `frame_map` parameter mirrors render_flags so the two stay in sync. In
// target view the flags paint at translated positions, so this helper is
// called with a non-null frame_map and the hit-rects walk the same map (see
// app_state's hit-test path). In source and render view it is null and
// positions are untranslated.
std::vector<FlagHitRect> compute_flag_hit_rects(
    GuiRect top_strip_area,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<FrameMapSegment>* frame_map = nullptr,
    const DragOverlay* drag_overlay = nullptr,
    bool iteration_on = false);

// Phase reset marker analogues. Same pixel layout as the warp
// versions; the visual differences are which list is drawn (phase resets
// instead of warp markers) and the supplied color set. `disabled` is taken
// directly from each phase reset (no label-cascade like warp markers).
void render_phase_reset_markers(cairo_t* cr,
                              GuiRect waveform_area,
                              const std::vector<GuiPhaseResetMarker>& phase_resets,
                              long long viewport_start_sample,
                              long long viewport_end_sample,
                              int sample_rate,
                              const std::set<int>& selected_set,
                              const std::vector<FrameMapSegment>* frame_map = nullptr,
                              const DragOverlay* drag_overlay = nullptr,
                              cairo_surface_t* ink_plate = nullptr);

// The phase-reset chip is an invariable single `p` (the peak/heap/pass
// phase-MODEL concept was removed once heap became the sole engine). Two
// states only: default fill `kMarker`, selected fill `kSelected`. No editor,
// no parse-fail state. Trim membership has no effect — flags always
// paint full-brightness.
void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            int sample_rate,
                            double font_size,
                            const std::set<int>& selected_set,
                            const std::vector<FrameMapSegment>* frame_map = nullptr,
                            const DragOverlay* drag_overlay = nullptr);

std::vector<FlagHitRect> compute_phase_reset_flag_hit_rects(
    GuiRect top_strip_area,
    const std::vector<GuiPhaseResetMarker>& phase_resets,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<FrameMapSegment>* frame_map = nullptr,
    const DragOverlay* drag_overlay = nullptr);

// Returns the text that render_flags would draw for `markers[idx]`. Used
// by the GUI text editor to seed the editable payload (the on-screen rect
// content) when entering edit mode on a flag.
std::string flag_text_for_marker(const std::vector<GuiWarpMarker>& markers, int idx);

// Iteration-aware sibling of flag_text_for_marker. Returns the
// plain flag text when `iteration_on` is false or the marker is iter-
// ineligible; otherwise splices the inline `+[lo, hi]` bracket after
// `tempo_base`. The single canonical composer for warp flag text — used
// by render_flags / render_one_editor_flag / compute_flag_hit_rects and
// to seed the flag editor in iteration mode.
std::string flag_text_iter(const std::vector<GuiWarpMarker>& markers,
                           int idx, bool iteration_on);

// Per-character pixel advance for the monospace font at kFlagFontSize.
// Measured once at startup via init_monospace_grid_metrics(); returns
// 0 if not yet measured. Used by click-to-position-cursor in the
// editor (input_handler.cpp -> flag_editor.cpp).
double monospace_advance();

// Fixed-pixel row height for the strip grid, measured from cairo_font_extents
// (ascent + descent) at kFlagFontSize plus 2*kFlagPadYPx.
// Returns kRowHFallbackPx until init_monospace_grid_metrics has measured the
// real font. The vertical twin of monospace_advance(); consumed by the
// strip/row geometry helpers (which have no cairo context of their own).
int monospace_row_h();

// Baseline offset from a row rect's top edge: kFlagPadYPx
// + font ascent. baseline_y = row.y + monospace_row_baseline_offset() centers
// the text in the row the same way the flag chip sits in the top strip.
double monospace_row_baseline_offset();

// Measure and cache the advance width. Idempotent. Called once at
// GUI startup after the cairo context exists. The supplied cairo_t*
// is used only for measurement; the font state is restored on return.
void init_monospace_grid_metrics(cairo_t* cr);

// Returns the on-screen x where the given marker's flag pending text
// starts, in pixels. Includes the kFlagPadXPx left inner pad so
// the returned value matches where pending text actually paints
// (cairo_move_to(cr, e.text_left + hl_pad, ...) at render.cpp:620).
// Returns -1.0 if the marker is not currently visible in the
// viewport. Direct computation -- does not require a cairo context.
double flag_pending_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx);

// One-line toggle for the render-path perf instrumentation. When
// false, all perf_counters increments and [dbg perf] stderr emissions in
// the redraw path are compiled out.
constexpr bool kDebugPerf = false;

// Diagnostic (F-flaggeom follow-up): when true, on_redraw strokes the flag
// hit rectangles — recomputed via the SAME path hit_test_flag uses — over the
// painted chips, so any paint-vs-hit coordinate divergence is visible. Off in
// normal builds. Remove once the edge-alignment diagnosis is complete.
constexpr bool kDebugHitRects = false;

// Hot-loop counters for perf instrumentation. Incremented by the render
// helpers on every relevant inner-loop step; the caller zeroes them with
// perf_counters::reset() before a measured pass and reads the totals
// afterwards. Single-threaded, no synchronization.
//
// The wf_cols / wf_pyramid_samples increments may
// fire from the waveform worker thread when kDebugPerf=true. The
// counters are not thread-safe — diagnostic use only.
namespace perf_counters {
    extern int wf_cols;              // pixel columns drawn by render_waveform
    extern int wf_pyramid_samples;   // peak-pyramid samples read
    extern int flag_measure;         // cairo_text_extents calls in flag render
    extern int flag_drawn;           // flags emitted (not elided)
    extern int flag_elided;          // viewport-hit flags skipped by greedy pack
    inline void reset() {
        wf_cols = 0;
        wf_pyramid_samples = 0;
        flag_measure = 0;
        flag_drawn = 0;
        flag_elided = 0;
    }
}
