#pragma once
#include "warpmarkers.h"
#include "phase_reset_markers.h"
#include "engine/stft_container.h"   // TimeMapSegment for target-view waveform

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

// Trim boundaries in source-frame samples. Threaded through the marker /
// flag renderers so they can apply uniform out-of-trim dimming under the
// brief H palette consolidation. Values match the convention in
// compute_trim_samples (main.cpp).
struct TrimRange {
    int64_t begin;
    int64_t end;
};

// Brief H palette: bases shared across the renderer module and
// main.cpp. kPlayheadCursor and kPlayheadScanner are foreground
// references and must never be passed to dim() — preserve that
// invariant in subsequent phases.
inline constexpr GuiColor kBackground       = {0.10, 0.10, 0.12};
inline constexpr GuiColor kWaveform         = {0.55, 0.75, 0.90};
inline constexpr GuiColor kMarker           = {0.57, 0.27, 0.68};
inline constexpr GuiColor kSelected         = {0.239, 0.682, 0.914};  // #3DAEE9 Breeze blue
inline constexpr GuiColor kPlayheadScanner  = {0.95, 0.85, 0.35};
inline constexpr GuiColor kPlayheadCursor   = {0.10, 0.74, 0.61};
inline constexpr GuiColor kAccent           = {0.75, 0.20, 0.18};
inline constexpr GuiColor kText             = {0.99, 0.99, 0.99};

// Flag rect geometry. Internal padding around the text glyph bounding box,
// applied symmetrically (horizontal and vertical). Brief Q raised this from
// 2 to 3 for breathing room. The single source of truth — both render and
// hit-rect computation must use this value, and so must the iteration popup
// in main.cpp.
constexpr double kFlagInnerPadPx = 3.0;

// Extra vertical inner padding added on top of kFlagInnerPadPx on each side.
// (V.B Addendum 2: rects grow by 2*kVPadExtraPx in height; the horizontal
// pad is unaffected.) Was previously file-private to render.cpp; moved
// here in Brief Q to share with main.cpp's iteration popup.
constexpr double kVPadExtraPx = 1.0;

// Vertical offset from the waveform area's top edge up to the flag rect's
// painted bottom edge. The flag rect sits in overlay space above the
// waveform; this is the height of its footprint there. The marker stem
// emanates from the rect's left outline (bottom edge) and runs down to
// the waveform bottom, so this constant also defines how far the stem
// extends above the waveform. Consumed by render_flags' baseline
// computation, by render_markers' stem geometry (via the equal-by-
// construction kStemAboveWaveformPx), and by the iter/BPM popups in
// main.cpp which mirror the flag rect's vertical position.
constexpr double kFlagBottomLiftPx = 11.0;

// Distance the marker stem extends above the waveform area, from the
// waveform's top edge up to the stem's top. By construction this equals
// kFlagBottomLiftPx — the stem starts at the flag rect's left outline
// (bottom edge, same column as the marker), so its top sits flush with
// the rect's bottom. Consumed by render_markers (stem geometry) and by
// the paint_handler stem blit (surface_h = area.h + kStemAboveWaveformPx,
// blit y = area.y - kStemAboveWaveformPx).
constexpr double kStemAboveWaveformPx = kFlagBottomLiftPx;

// The marker stem's painted top sits ONE pixel below the overlay band's
// boundary row (area.y - kStemAboveWaveformPx). That boundary row
// (area.y - kFlagBottomLiftPx) is the flag rect's bottom-border row,
// painted by the selection outline only when the marker is selected.
// Stopping the stem one row short leaves that row to the border: when
// selected the border owns it; when unselected it stays empty (and the
// cursor triangle's top covers area.y - 10). The stem cache surface
// overhang stays kStemAboveWaveformPx tall — only the stroke's starting
// row moves; see render.cpp's y_stem_top.
constexpr double kStemPaintTopPx = kStemAboveWaveformPx - 1.0;

// Editor pixel size for the flag-payload editor, iter popup, and BPM
// popup. Computed as 11 pt at 96 DPI (the conventional Linux default
// at non-HiDPI). warptempo_gui does not currently support HiDPI; this is
// a fixed pixel value rather than a runtime pt->px conversion. The
// literal computation form makes the pt origin self-documenting --
// the compiler folds it to a constant at compile time.
constexpr double kFlagFontSize = 11.0 * 96.0 / 72.0;

// Half-blend toward background. The single derivation function for
// "subordinate" / "out-of-trim" state under the new palette.
constexpr GuiColor dim(GuiColor c) {
    return GuiColor{
        c.r * 0.5 + kBackground.r * 0.5,
        c.g * 0.5 + kBackground.g * 0.5,
        c.b * 0.5 + kBackground.b * 0.5,
    };
}

// Brief Y.4 sub-bug A: paints an opaque kBackground-colored rect under
// flag and iter popup text glyphs so the editor's growing pending text
// occludes neighbor text rather than blending with it. Without this fill,
// static flag/popup text paints directly on the canvas, and a widening
// edit shares pixels with adjacent flags' glyphs (both sets are visible
// blended). The fill matches the strip-clear color exactly, so in every
// non-edit state nothing changes visually; during an edit it does the
// occlusion work once pending text widens past the original flag width.
//
// Drawn before any outline (selection purple, editor parse-fail red) and
// before the text glyphs.
//
// `text_left` is the actual text painting x — i.e., where cairo_move_to
// would place the cursor for cairo_show_text. The helper subtracts
// kFlagInnerPadPx itself to derive the fill rect's left edge. `bg_top`
// and `bg_height` reuse the existing outline/highlight rect math at the
// caller, so the fill aligns with the outline that gets painted on top.
inline void render_flag_text_bg_fill(cairo_t* cr,
                                     double text_left,
                                     double text_x_advance,
                                     double bg_top,
                                     double bg_height,
                                     GuiColor fill) {
    const double pad = kFlagInnerPadPx;
    const double x = std::round(text_left - pad);
    const double y = std::round(bg_top);
    const double w = std::round(text_x_advance + 2.0 * pad);
    const double h = std::round(bg_height);
    if (w <= 0.0 || h <= 0.0) return;
    cairo_save(cr);
    cairo_set_source_rgb(cr, fill.r, fill.g, fill.b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_restore(cr);
}

// Brief B.2 editor text-box primitive. Draws the full editable-text-box
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
// region (the prefix, if any, sits to its left on the canvas), via
// render_flag_text_bg_fill keyed off `uniform_ext` for a content-
// independent box height. The cursor uses the std::round(x)+0.5 half-pixel
// convention for a crisp single-pixel column.
//
// Colors are pre-resolved by the caller: `fill` already has dim() applied
// when out-of-trim, and `text_color` is kText (or dim(kText)). The
// selection swap fills the selected range with `text_color` and repaints
// the selected substring in `fill` for contrast.
struct EditorTextBox {
    double               anchor_x        = 0.0;
    double               baseline_y      = 0.0;
    std::string          prefix;            // optional; "" = none
    std::string          text;              // editable content
    cairo_text_extents_t uniform_ext       = {};  // box height/y_bearing ref
    double               hl_pad           = kFlagInnerPadPx;
    GuiColor             fill             = kMarker;
    GuiColor             text_color       = kText;
    bool                 has_selection    = false;
    int                  selection_start  = 0;
    int                  selection_end    = 0;
    bool                 cursor_visible   = false;
    int                  cursor_pos       = 0;
};
void render_editor_text_box(cairo_t* cr, const EditorTextBox& s);

// Out-of-trim predicate. Caller computes source-frame position from its
// own native field (time_seconds*sample_rate for both GuiWarpMarker and
// GuiPhaseResetMarker) and passes it through here.
// The trim is treated as the closed interval [begin, end] for the dim-
// vs-active flag-color decision, so a marker landing exactly on the end
// boundary (the e=-marker itself) renders active, not dimmed. Other
// trim consumers (heatmap stripe, playback bounds, engine timemap) use
// their own direct comparisons against trim.begin / trim.end and are
// unaffected by this predicate.
inline bool marker_out_of_trim(int64_t source_frame_pos,
                               const TrimRange& trim) {
    return source_frame_pos < trim.begin || source_frame_pos > trim.end;
}

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

void render_progress_bar(cairo_t* cr, int x, int y, int w, int h,
                         float progress_fraction);

// Draws one channel's waveform into `area`, displaying samples in
// [viewport_start_sample, viewport_end_sample). When `timemap` is null
// (source view) the viewport range is interpreted in source-frame and
// each column reads `audio.get_peak_range` directly. When `timemap` is
// non-null (target view) the viewport range is target-frame: each
// column's [t0, t1) is translated to source-frame via
// `map_target_to_source` before the pyramid read, producing the
// deformed-waveform display. The brightness threshold (trim_*_sample)
// is interpreted in the SAME domain as the viewport: source-frame in
// source view, target-frame in target view.
//
// Columns whose midpoint falls inside [trim_begin_sample,
// trim_end_sample) paint with `bright_color`; the rest paint with
// `dim_color`. Pass a wide range to disable dimming.
void render_waveform(cairo_t* cr,
                     GuiRect area,
                     const GuiAudio& audio,
                     int channel,
                     long long viewport_start_sample,
                     long long viewport_end_sample,
                     long long trim_begin_sample,
                     long long trim_end_sample,
                     GuiColor bright_color,
                     GuiColor dim_color,
                     const std::vector<TimeMapSegment>* timemap = nullptr);

// Draws a thin 1px vertical line across `area` at column `playhead_pixel_x`
// (offset from area.x, float for subpixel centering). No-op if outside.
// `triangle_surface` is the pre-loaded playhead-triangle indicator (loaded by
// GuiPlatform); it's stamped above the stem via cairo_mask_surface, tinted with
// `color`. May be nullptr — in that case the indicator is skipped. The
// triangle belongs to the cursor exclusively under the split-playhead
// model; pass `draw_triangle = false` for the scanner call so only the
// vertical line is drawn.
void render_playhead(cairo_t* cr,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color,
                     cairo_surface_t* triangle_surface,
                     bool draw_triangle = true);

// Draws vertical 1-pixel lines across `waveform_area` for each marker whose
// resolved sample falls inside [viewport_start_sample, viewport_end_sample).
// Effective disabled state is computed inline from the marker list (a label
// reference inherits the disabled flag of its defining marker). Disabled
// markers are skipped entirely; selection has no effect on stems under the
// brief H palette rules.
// `timemap` (default null) shifts marker positioning into the target-frame
// domain when target view is active: each marker's source-frame position is
// run through `map_source_to_target` before viewport clipping and column
// placement. Trim brightness uses the translated position so it stays
// consistent with `trim` (which paint_handler also forwards in the same
// domain). Null timemap = identity, exact pre-brief-2 behavior.
void render_markers(cairo_t* cr,
                    GuiRect waveform_area,
                    const std::vector<GuiWarpMarker>& markers,
                    long long viewport_start_sample,
                    long long viewport_end_sample,
                    int sample_rate,
                    const TrimRange& trim,
                    const std::set<int>& selected_set,
                    const std::vector<TimeMapSegment>* timemap = nullptr,
                    const DragOverlay* drag_overlay = nullptr);

// Editor overlay used by V.A1's top-flag editor. When `marker_index >= 0`
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
    // Brief seven: selection range within `pending`. When
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
// Brief H three-state model: each flag renders in one of three states.
//   1. Not selected: text in `kText`, no background fill.
//   2. Selected, editor not engaged: background fill in `kMarker`, text
//      in `kText`. No cursor.
//   3. Selected, editor engaged: state 2 plus a 1-px blinking cursor.
// Parse-fail variant of state 2/3: fill is `kAccent` instead of `kMarker`.
// Markers whose source-frame position lies outside `trim` wrap every
// color in `dim()` uniformly — no element of the flag escapes the dim.
//
// Disabled markers render identically to enabled markers in the top strip;
// the only disabled signal lives in the marker stem (handled by
// `render_markers`).
// `timemap`: same target-frame translation as render_markers. The greedy
// pack and elision still walk left-to-right, so in target view the pack
// rule is applied against post-translation positions (a region the timemap
// stretches may un-elide flags that were elided in source view; a region
// the timemap compresses may elide flags that were visible there).
void render_flags(cairo_t* cr,
                  GuiRect top_strip_area,
                  const std::vector<GuiWarpMarker>& markers,
                  long long viewport_start_sample,
                  long long viewport_end_sample,
                  int sample_rate,
                  double font_size,
                  const std::set<int>& selected_set,
                  const TrimRange& trim,
                  const FlagEditorOverlay& editor = {},
                  const std::vector<TimeMapSegment>* timemap = nullptr,
                  const DragOverlay* drag_overlay = nullptr);

// Stage C: paints ONE flag — the FlagPayload-editor target — with the
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
    const TrimRange& trim,
    const FlagEditorOverlay& editor,
    const std::vector<TimeMapSegment>* timemap = nullptr,
    const DragOverlay* drag_overlay = nullptr);

// Same greedy-pack and elision logic as render_flags, without drawing —
// returns the screen-coord rects of the flags that would be rendered. The
// caller uses these for hit-testing. A minimal image-surface cairo_t works
// fine as `cr` since only font metrics are needed.
// `timemap` parameter mirrors render_flags so the two stay in sync. Brief 2
// adds it for symmetry — target view's hover/iter/BPM popups are gated off
// elsewhere, so this helper is not yet called with a non-null timemap. A
// future brief that re-enables popup paint in target view will route the
// timemap through here without further signature churn.
std::vector<FlagHitRect> compute_flag_hit_rects(
    cairo_t* cr,
    GuiRect top_strip_area,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap = nullptr,
    const DragOverlay* drag_overlay = nullptr);

// Phase reset marker analogues (chunk S.2.2). Same pixel layout as the warp
// versions; the visual differences are which list is drawn (phase resets
// instead of warp markers) and the supplied color set. `disabled` is taken
// directly from each phase reset (no label-cascade like warp markers).
void render_phase_reset_markers(cairo_t* cr,
                              GuiRect waveform_area,
                              const std::vector<GuiPhaseResetMarker>& phase_resets,
                              long long viewport_start_sample,
                              long long viewport_end_sample,
                              int sample_rate,
                              const TrimRange& trim,
                              const std::set<int>& selected_set,
                              const std::vector<TimeMapSegment>* timemap = nullptr,
                              const DragOverlay* drag_overlay = nullptr);

// Flag text for phase resets is `[b=|e=]<status>` where status is `I`
// (inserted), `D` (detected), or `D*` (detected with user displacement).
//
// Brief H two-state model (no flag editor exists for phase resets):
//   1. Not selected: text in `kText`, no background fill.
//   2. Selected: background fill in `kMarker`, text in `kText`.
// Markers whose time_seconds (converted to source frames) lies outside
// `trim` wrap every color in `dim()` uniformly.
void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            int sample_rate,
                            double font_size,
                            const std::set<int>& selected_set,
                            const TrimRange& trim,
                            const std::vector<TimeMapSegment>* timemap = nullptr,
                            const DragOverlay* drag_overlay = nullptr);

std::vector<FlagHitRect> compute_phase_reset_flag_hit_rects(
    cairo_t* cr,
    GuiRect top_strip_area,
    const std::vector<GuiPhaseResetMarker>& phase_resets,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap = nullptr,
    const DragOverlay* drag_overlay = nullptr);

// Returns the text that render_flags would draw for `markers[idx]`. Used
// by the GUI text editor to seed the editable payload (the on-screen rect
// content) when entering edit mode on a flag.
std::string flag_text_for_marker(const std::vector<GuiWarpMarker>& markers, int idx);

// Walks backward from `index` through `markers` to find the nearest marker
// that owns its tempo (tempo_inherits == false and not a label reference).
// Returns 1.0 if no such marker exists (shouldn't happen given the time-0
// invariant, but defensive for edge cases during authoring).
double resolve_inherited_tempo(const std::vector<GuiWarpMarker>& markers, int index);

// Companion to resolve_inherited_tempo: returns the scale string of the
// inherited source, or "" if none.
std::string resolve_inherited_tempo_scale(
    const std::vector<GuiWarpMarker>& markers, int index);

// X.7.8b-3: promoted out of main.cpp's anonymous namespace so
// input_handler.cpp can reach it from the on_motion body.
//
// V.A3b hover-popup text. Computes the same resolution math the engine
// uses when emitting the .timemap, so the popup matches what the engine
// will produce. Pass markers emit "= TEMPO" or "= TEMPO*SCALE" (single
// equals; resolved tempo of the nearest prior owning marker). Label_ref
// markers emit "~= BASE*COMBINED_SCALE" (tilde-equals, mirroring engine
// behavior). BASE is rendered at 2 decimals; COMBINED_SCALE is
// `def_scale * multiplier` when the def has a typed scale, else just
// `multiplier`, rendered at 4 decimals. Returns "" when the marker
// doesn't qualify for a hover popup (owning, missing def, malformed).
//
// Sibling to resolve_inherited_tempo / flag_text_for_marker — same
// rendering-time text formatting role over GuiWarpMarker, same TU.
std::string compute_hover_popup_text(
    const std::vector<GuiWarpMarker>& mv, int idx, int sample_rate);

// Per-character pixel advance for the monospace font at kFlagFontSize.
// Measured once at startup via init_monospace_grid_metrics(); returns
// 0 if not yet measured. Used by click-to-position-cursor in the
// editor (input_handler.cpp -> flag_editor.cpp).
double monospace_advance();

// Measure and cache the advance width. Idempotent. Called once at
// GUI startup after the cairo context exists. The supplied cairo_t*
// is used only for measurement; the font state is restored on return.
void init_monospace_grid_metrics(cairo_t* cr);

// Returns the on-screen x where the given marker's flag pending text
// starts, in pixels. Includes the kFlagInnerPadPx left inner pad so
// the returned value matches where pending text actually paints
// (cairo_move_to(cr, e.text_left + hl_pad, ...) at render.cpp:620).
// Returns -1.0 if the marker is not currently visible in the
// viewport. Direct computation -- does not require a cairo context.
double flag_pending_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx);

// One-line toggle for the render-path perf instrumentation (chunk M). When
// false, all perf_counters increments and [dbg perf] stderr emissions in
// the redraw path are compiled out.
constexpr bool kDebugPerf = false;

// Hot-loop counters for perf instrumentation. Incremented by the render
// helpers on every relevant inner-loop step; the caller zeroes them with
// perf_counters::reset() before a measured pass and reads the totals
// afterwards. Single-threaded, no synchronization.
//
// Stage A and beyond: the wf_cols / wf_pyramid_samples increments may
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
