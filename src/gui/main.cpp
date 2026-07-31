// warptempo_gui - phase vocoder for time-warping classical orchestral
// recordings toward target tempos.
//
// Copyright (C) 2024-2026  warptempo
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>.

#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "color_config.h"
#include "env_fingerprint.h"
#include "waveform_worker.h"
#include "warpmarkers.h"
#include "file_loader.h"
#include "flag_editor.h"
#include "gui_display_context.h"
#include "input_handler.h"
#include "paint_handler.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "render.h"
#include "render_pipeline.h"
#include "renders_dir.h"
#include "active_views.h"
#include "save_ops.h"
#include "selection.h"
#include "settings_editor.h"
#include "settings_io.h"
#include "render_cache.h"
#include "target_render.h"
#include "text_display.h"
#include "text_editor.h"
#include "time_format.h"
#include "phaseresetmarkers.h"
#include "phaseresetmarkers_ops.h"
#include "prompt.h"
#include "undo.h"
#include "viewport.h"
#include "warpmarkers_ops.h"
#include "platform_wayland.h"
#include "locale_check.h"

#include <cairo/cairo.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

// flag_font_size_px() (render.h) and timestamp_pad_x() (paint_handler.h)
// live where paint_handler.cpp can reach them; the constants
// below are paint-handler-independent and stay file-local.

// The strip/lane geometry is a fixed-pixel per-strip lane stack derived from
// menu_row_h_px(), toolbar_row_h_px(), flag_lane_w_px() / flag_lane_h_px(),
// monospace_text_row_h(), playhead_triangle_h_px(), kRowGapPx, and
// kFlagBottomLiftPx (see the geometry helpers below); nothing is
// window-proportional.

// kMarkerHitHalfPx lives in app_state.h so the hit_test_* free
// functions and the GuiInputHandler mouse handler can reach it.

// ms-per-pixel is a continuous function of the zoom level (a real-valued
// exponent): ms_per_px(level) = 0.625 * 2^(level - 1), computed directly in
// samples_per_pixel_at. The level rests anywhere in the one continuous domain
// [kMinZoom, kMaxZoom] — no sentinel. Level 1 is the deepest zoom-in (0.625
// ms/px, 1.2 s); each whole step is exactly 2x the previous, so the integer
// rungs reproduce the historical ladder (0.625, 1.25, 2.5, ...) bit-for-bit,
// and the fit-equivalent level (full zoom-out, whole song visible) is just the
// point on the same curve where spp * width == total.

// playhead_half_px() (half-width of the column invalidated around a playhead
// position) now lives in render.h as a single shared inline accessor,
// reached here via the render.h include.

// The BPM-sweep math primitive (BaseTempoScale + compute_base_tempo_scale)
// lives in input_handler.h so input_handler.cpp can reach it.
// GuiInputHandler::render_bpm_sweep is the sole caller.

// compute_hover_popup_text lives in the parser (warp_frame_map_build.{cpp,h})
// and operates on the parser's WarpMarker. It is a different translation
// unit from the GUI flag-text composer (flag_text_iter), which stays in
// render.cpp over GuiWarpMarker.

// UndoEntry, DragState, UndoHistory, RegionState, RegionDragState,
// HoverPopupState, DialogTrigger, PromptState, ViewState,
// AppState live in app_state.h, alongside the Viewport struct.


// The settings format / write helpers live in settings_io.{h,cpp} so
// file_loader.cpp and save_markers can both reach them; the .settings
// reader is the parser-side schema (settings_file.h), shared with the CLI.

// WaveformCache was promoted to paint_handler.{h,cpp} so paint_handler.cpp can
// reach it. The instance is still a local in main() and is passed by reference
// into GuiPaintHandler.

} // namespace

// Geometry helpers — public to viewport.cpp via app_state.h.
// samples_per_pixel_at is likewise declared in app_state.h and defined
// non-static: it is called from input_render_dispatch.cpp (the queue/dispatch
// view-anchor math), so it cannot be main-private.
//
// Per-strip lane stacks with per-lane heights (the former uniform-row contract
// is superseded). Top and bottom strips now DIFFER in height; the waveform
// flexes between them. The TOP strip is SIX lanes (from the window edge
// inward): MENU ROW (its own authored menu_row_h_px(), row 1 of the kdenlive
// redesign), TOOLBAR ROW (its own authored toolbar_row_h_px(), row 2 of the
// redesign — these two are the lanes on the gui_scale axis), trim-chip row (the
// flag WIDTH, so the chips are square), marker-text row (the text-lane
// monospace_text_row_h()), flag row (the flag height), and the triangle row
// (the triangle height) flush on the waveform. The BOTTOM strip is TWO lanes:
// the status row (outer) and the editor/modal row (inner), both text lanes.
// The lanes pack tight — the inter-lane gaps kRowGapPx and the
// outer/waveform-side gaps
// kFlagBottomLiftPx are all 0 — and the derivation below keeps them explicit so
// the stack reappears correct if either constant is ever un-zeroed. That
// property has NO EXCEPTION LIST among the consumers: every lane-anchored pixel
// reaches its band through a lane rect computed here. The
// render.cpp sites that used to stack BOTTOM-ANCHORED off the waveform
// top edge — flag_lane_geometry (the shared owner of the painted marker shapes
// AND their hit rects) and the triangle blit in render_playhead — now take the
// flag and triangle LANE RECTS as
// parameters, resolved by their callers from top_flag_row_area /
// top_triangle_row_area, the same rects the empty-lane press gate tests. So the
// painted flags, the flag hit rects, and the playhead triangle all move with
// the lanes by construction, and marker paint and marker hit-testing stay
// coherent with each other as they always did, both riding
// flag_lane_geometry.
//
// ONE SEAM IS EXEMPT from a hypothetical nonzero kRowGapPx: the FLAG/TRIANGLE
// junction (top lanes 4|5). A marker flag is ONE FUSED GLYPH — rectangle plus
// tip-down triangle under a single continuous outline — that spans those two
// lanes, so they are CONTIGUOUS BY INVARIANT and flag_lane_geometry (the reader
// of that junction, render.cpp) takes the rectangle's bottom edge straight from
// the triangle lane's TOP. A gap there would cut one asset through its middle:
// unsupported, a design error rather than a layout choice, and the full record
// of the consequence lives at flag_lane_geometry. Gaps at every OTHER seam —
// menu|toolbar, toolbar|chip, chip|text, text|flag, and both outer
// kFlagBottomLiftPx gaps —
// are honored structurally by the loop below and by every consumer. ONE shared
// helper —
// strip_row_rect — is the single geometry owner; every named accessor delegates
// to it, so a lane is a pure index from its strip's window edge, and a bottom
// lane's y is its inset flipped about the window midline.

// Defensive backstop only: floor the window dims to the 640x480 minimum before
// any geometry arithmetic so no code path can compute a negative/zero waveform,
// regardless of what the compositor sends. Mirrors the set_min_size hint.
static void clamp_dims(int& w, int& h) {
    if (w < kMinWindowWidthPx)  w = kMinWindowWidthPx;
    if (h < kMinWindowHeightPx) h = kMinWindowHeightPx;
}

namespace {
// Per-lane pixel heights, indexed from each strip's window edge inward. Every
// lane sizes from the metric that DESCRIBES it: the flag row from the flag
// height, the innermost lane from the triangle height, and the trim-chip row
// from the flag WIDTH (see the case below). The TEXT-BEARING lanes — the
// marker-text row and both bottom lanes — carry the LANE metric, which is the
// text box (glyph band, ring, four-side pad) plus the vertical margin outside
// its ring; the box itself is shorter and is placed inside this band by
// flag_chip_rect.
//
// LANES 0 AND 1 ARE THE EXCEPTION to "the metric that describes it" being a
// FONT metric: the two REDESIGNED rows carry proportional text at a fixed
// design size and size from their own authored constants on the GUI-SCALE axis
// (menu_row_h_px(), toolbar_row_h_px()), not the monospace font's — the
// two-axes ruling is at those accessors' declarations in render.h. The toolbar
// lane INCLUDES its 1px border-bottom (the CSS box model: the architect's
// stated 44 is content, the border sits outside it, and the lane owns both).
constexpr int kTopLaneCount    = 6;
constexpr int kBottomLaneCount = 2;
int top_lane_height(int lane) {
    switch (lane) {
        case 0: return menu_row_h_px();          // menu row (proportional text)
        case 1: return toolbar_row_h_px();       // toolbar row (+ border-bottom)
        // The trim-chip row takes the chip's own WIDTH as its height, so the
        // b/e chips are SQUARE BY CONSTRUCTION: trim_chip_rect reads its width
        // from flag_lane_w_px() and its height from this band, and both axes now
        // resolve through the one accessor. A plain 15 here, or the flag height
        // the row used to borrow, would leave squareness a numeric coincidence
        // that a retune of kFlagHeightPx (or of the scaling) could silently
        // break.
        case 2: return flag_lane_w_px();         // trim-chip row (square chips)
        case 3: return monospace_text_row_h();   // marker-text row
        case 4: return flag_lane_h_px();         // flag row
        case 5: return playhead_triangle_h_px(); // triangle row (flush on waveform)
        default: return 0;
    }
}
int bottom_lane_height() {
    return monospace_text_row_h();               // status row, editor/modal row
}
int strip_total_h(bool top_strip) {
    int sum = 2 * static_cast<int>(kFlagBottomLiftPx);  // outer + waveform-side gaps
    const int lanes = top_strip ? kTopLaneCount : kBottomLaneCount;
    for (int i = 0; i < lanes; ++i)
        sum += top_strip ? top_lane_height(i) : bottom_lane_height();
    sum += (lanes - 1) * static_cast<int>(kRowGapPx);   // inter-lane gaps
    return sum;
}
} // namespace

int top_strip_h(const AppState&)    { return strip_total_h(/*top_strip=*/true);  }
int bottom_strip_h(const AppState&) { return strip_total_h(/*top_strip=*/false); }

GuiRect top_strip_area(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    return GuiRect{0, 0, w, top_strip_h(a)};
}

GuiRect bottom_strip_area(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    const int sh = bottom_strip_h(a);
    return GuiRect{0, h - sh, w, sh};
}

GuiRect waveform_area(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    const int top_h = top_strip_h(a);
    const int bot_h = bottom_strip_h(a);
    // Effective waveform width: the largest multiple of the grid step not
    // exceeding the window width, leaving a <=15 px inert right gutter. The
    // step is 16 = 1600/gcd(44100,1600), the strictest step among standard
    // sample rates (every standard rate's step divides 16), so at a
    // multiple-of-16 width logical_spp·W is integral at every INTEGER zoom
    // rung and painter samples-per-pixel equals the logical spp exactly there
    // — the grid the pixel-anchored commits and the migration tool both
    // target. A fractional rung (the continuous strip-drag zoom) has no such
    // integral guarantee; painter_samples_per_pixel rides its own
    // nearbyint(spp·W)/W quantization instead, so the authoring-grid
    // bit-exactness claim above is scoped to the integer rungs, notably the
    // working zoom. A gutter appears only at a non-multiple-of-16 width
    // (never at 1920/2560/3840).
    constexpr int kGridStepPx = 16;
    const int effective_w = w - (w % kGridStepPx);
    return GuiRect{0, top_h, effective_w, h - top_h - bot_h};
}

// ONE shared layout contract for every strip lane — the single geometry owner.
// A lane is a pure index from its strip's window edge (0 = the edge-most lane):
// the outer gap kFlagBottomLiftPx sits between the window edge and lane 0, and
// each successive lane is one prior-lane height + one inter-lane gap kRowGapPx
// further inward. The top strip counts downward from y=0; the bottom strip
// mirrors it about the window midline (`h - inset - lane_h`).
//
// Paint/hit agreement invariant: the trim-chip row is TOP lane 2, and
// hit_test_trim_chip / the pair-drag y-gate read top_upper_row_area(app) — the
// exact band render_trim_flags paints the b/e chips in, because the PAINTER is
// handed that band as a parameter (GuiPaintHandler::paint_trim passes
// top_upper_row_area(app) as render_trim_flags' `chip_row`) instead of
// re-deriving a lane y from the row heights above it. Both sides therefore
// reach the band through this one helper, so paint and hit cannot drift when a
// lane above the chip row changes height, is removed, or gains a gap.
GuiRect strip_row_rect(const AppState& a, bool top_strip,
                       int lane_from_window_edge) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    int inset = static_cast<int>(kFlagBottomLiftPx);
    for (int i = 0; i < lane_from_window_edge; ++i) {
        inset += top_strip ? top_lane_height(i) : bottom_lane_height();
        inset += static_cast<int>(kRowGapPx);
    }
    const int lane_h = top_strip ? top_lane_height(lane_from_window_edge)
                                 : bottom_lane_height();
    const int y = top_strip ? inset : (h - inset - lane_h);
    return GuiRect{0, y, w, lane_h};
}

// Top strip lanes, counted down from the window top (index 0 = the window edge).
// Lane 0 is the MENU row (the kdenlive menu bar: a flat ground carrying the Quit
// button, at the window edge); lane 1 is the TOOLBAR row (the flat ground
// carrying the Save / Undo / Redo / Render buttons, its separators and its
// border-bottom); lane 2 is the b/e trim-chip row, which also owns the
// span-framing double-click; lane 3 is the marker-text
// row (hosts the hover popup and the flag editor's live text, one at a time —
// paint_marker_text_lane); lane 4 is the
// flag row (the marker flag rectangles); lane 5 is the triangle row, whose
// bottom edge is flush with the waveform area top and which holds the flags' and
// the playhead's triangles.
GuiRect top_menu_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 0);
}

GuiRect top_toolbar_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 1);
}

GuiRect top_upper_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 2);
}

GuiRect top_marker_text_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 3);
}

GuiRect top_flag_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 4);
}

GuiRect top_triangle_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 5);
}

// Bottom strip lanes, counted up from the window bottom (index 0 = the window
// edge). bottom_lower_row (outer, index 0) carries the always-on status line;
// bottom_upper_row (inner, index 1) carries the modal/editor/queue chain and,
// below it, the pass/ref resolved hover readout (a marker's own value shows in
// the top strip's marker-text lane).
// (The former pan-strip row retired — pan lives on the Alt+drag waveform grab
// and the ctrl+waveform strip drag's horizontal axis.)
GuiRect bottom_upper_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/false, 1);
}

GuiRect bottom_lower_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/false, 0);
}

// Resolve the SOURCE-view trim playback/navigation range from AppState's trim
// pair — one of the TWO range owners (Viewport::trim_range is the target-view
// half), and the reason the full window behaves exactly as the old unset state
// did everywhere downstream.
//
// THE FULL-WINDOW NORMALIZATION (architect 2026-07-30): the store's end bound
// is an INCLUSIVE authored frame while this range's end is EXCLUSIVE, so a full
// window [0, total-1] taken literally would return {0, total-1} and silently
// drop the last source frame — End would land at total-2 and the last legal
// near-end launch would go inert. A full pair therefore normalizes to
// {0, total_frames}, the exact pair the retired unset state produced. The
// recognition is the shared owner trim_window_is_full (settings_file.h), never
// a second compare. A PROPER sub-window keeps today's arithmetic verbatim.
//
// The stored bounds are already whole int64 frames; each side clamps to
// [0, total_frames] independently so playback ranges stay inside the buffer.
// There is NO ordering clamp: the pair passes through as authored. Crossed or
// equal bounds can no longer REST (the commit and load auto-resets reset such
// pairs to the full window), but MID-GESTURE crossing stays free and this runs
// per frame, so consumers (the Space gate's cursor-in-[begin,end) check,
// Home/End via trim_range, the load-time playhead) still degrade to a no-op or
// a per-side position and must not assume begin <= end.
std::pair<long long, long long> compute_trim_samples(
    const AppState& a, long long total_frames) {
    if (trim_window_is_full(a.trim.begin_frame, a.trim.end_frame,
                            total_frames)) {
        return {0, total_frames};
    }
    long long begin = a.trim.begin_frame;
    long long end   = a.trim.end_frame;
    if (begin < 0) begin = 0;
    if (begin > total_frames) begin = total_frames;
    if (end < 0) end = 0;
    if (end > total_frames) end = total_frames;
    return {begin, end};
}

double samples_per_pixel_at(double zoom_level, int sample_rate) {
    // One continuous domain, no sentinel: ms_per_px = 0.625 * 2^(level - 1).
    // Fully level-determined and domain-independent — at the per-file effective
    // ceiling the exponent already yields spp = total/width (whole song
    // visible) by construction, so there is no fit-file special case.
    assert(zoom_level >= kMinZoom && zoom_level <= kMaxZoom);
    return 0.625 * std::exp2(zoom_level - 1.0) *
           static_cast<double>(sample_rate) / 1000.0;
}

// The per-file effective zoom-out ceiling: the continuous level at which
// samples_visible == total_frames, clamped into [kMinZoom, kMaxZoom].
// samples_visible(L) = 0.625 * 2^(L-1) * sr/1000 * width == total solves to
// fit_level = 1 + log2(total * 1000 / (0.625 * sr * width)); full zoom-out
// rests here (whole-song-visible, Ableton behavior). A degenerate tiny file
// (fit_level < kMinZoom) collapses the range to the floor — clamp_viewport_start's
// visible >= total branch owns that start = 0 display. Because kMaxZoom is
// derived from audio_io's structural source caps (see settings_file.h), fit_level
// is below kMaxZoom for every loadable file, so the clamp's upper edge is never
// the binding one in practice.
double effective_max_zoom_level(int waveform_width_px,
                                int64_t total_frames,
                                int sample_rate) {
    if (waveform_width_px <= 0 || total_frames <= 0 || sample_rate <= 0)
        return kMinZoom;  // degenerate: collapse to the floor
    const double fit = 1.0 + std::log2(
        static_cast<double>(total_frames) * 1000.0 /
        (0.625 * static_cast<double>(sample_rate) *
         static_cast<double>(waveform_width_px)));
    return std::clamp(fit, kMinZoom, kMaxZoom);
}

int64_t live_total_frames(const AppState& a, const GuiAudio& audio) {
    // The active display context owns the composite view rule and the
    // target-total fallback (gui_display_context.h).
    return active_display_context(a, audio).domain_total_frames;
}

double clamp_zoom_level(const AppState& a, const GuiAudio& audio, double level) {
    // The one owner of the level-bounds pair. No live frames (loading states) →
    // return the level untouched: effective_max_zoom_level collapses to kMinZoom
    // on a zero total, and stomping the level the load path is mid-assignment
    // would be wrong. Otherwise clamp into [kMinZoom, per-file ceiling].
    const int64_t total = live_total_frames(a, audio);
    if (total <= 0) return level;
    return std::clamp(level, kMinZoom,
                      effective_max_zoom_level(waveform_area(a).w, total,
                                               audio.sample_rate()));
}

int64_t samples_visible(const AppState& a, const GuiAudio& audio) {
    const GuiRect area = waveform_area(a);
    // Preserved evaluation: the former signature evaluated live_total_frames
    // here, and in target view that primes target_view_warp_frame_map_cached (a
    // live map rebuild + the resolver's normalization stderr at this timing).
    // spp no longer needs the total, but dropping the evaluation would move the
    // cache-rebuild/diagnostic timing — deliberately kept identical.
    (void)live_total_frames(a, audio);
    const double spp = samples_per_pixel_at(a.zoom_level, audio.sample_rate());
    return static_cast<int64_t>(std::nearbyint(spp * area.w));
}

double current_samples_per_pixel(const AppState& a, const GuiAudio& audio) {
    // Preserved evaluation: the former signature evaluated live_total_frames
    // here, and in target view that primes target_view_warp_frame_map_cached (a
    // live map rebuild + the resolver's normalization stderr at this timing).
    // spp no longer needs the total, but dropping the evaluation would move the
    // cache-rebuild/diagnostic timing — deliberately kept identical.
    (void)live_total_frames(a, audio);
    return samples_per_pixel_at(a.zoom_level, audio.sample_rate());
}

std::pair<int64_t, int64_t> viewport_marker_bounds(const AppState& a,
                                                   const GuiAudio& audio) {
    const GuiRect area = waveform_area(a);
    const double  spp  = current_samples_per_pixel(a, audio);
    const int64_t lo   = a.viewport_start_sample;
    const int64_t hi   = a.viewport_start_sample +
        static_cast<int64_t>(std::nearbyint(
            static_cast<double>(area.w - 1) * spp));
    return { lo, hi };
}

int64_t max_viewport_start_grid(const AppState& a, const GuiAudio& audio) {
    // The rightmost ON-grid viewport start: the smallest painter-grid point
    // >= max_start (= total - visible). Grid points g(k)=nearbyint(k*q) are
    // strictly increasing at numeric zoom (q >> 1), so starting at
    // floor(max_start/q) and stepping up finds it in O(1). Resting here shows
    // <1 px of inert padding past EOF (subsumed in the last column; get_peak_range
    // clamps past-EOF reads to silence), so the flush-right viewport is a true
    // grid point — unlike the off-grid max_start it replaces, this keeps
    // exact-grid marker commits and pixel anchoring simultaneously valid at
    // maximum scroll.
    //
    // The ONE right-wall owner, shared by the resting clamp (clamp_viewport_start
    // below) and the strip drag's per-event pan clamp (apply_strip_drag_at): both
    // read the same live state, so a press at the flush-right rest derives its
    // virtual viewport at exactly the rest. Degenerate branches: visible >= total
    // (the whole song fits) → 0; q <= 0 (non-numeric zoom) → max(0, max_start).
    const int64_t visible = samples_visible(a, audio);
    const int64_t total   = live_total_frames(a, audio);
    if (visible >= total) return 0;
    const int64_t max_start = total - visible;
    const double  q = painter_samples_per_pixel(a, audio, waveform_area(a));
    if (q <= 0.0) return std::max<int64_t>(0, max_start);
    int64_t k = static_cast<int64_t>(std::floor(max_start / q));
    if (k < 0) k = 0;
    auto grid = [q](int64_t kk) {
        return static_cast<int64_t>(std::nearbyint(static_cast<double>(kk) * q));
    };
    while (grid(k) < max_start) ++k;
    return grid(k);
}

void clamp_viewport_start(AppState& a, const GuiAudio& audio) {
    // Level ceiling (the chokepoint): every zoom write funnels through this
    // function immediately after assigning zoom_level — the same single-funnel
    // argument that placed the viewport grid snap below — so a per-file ceiling
    // assigned above (0/c full-out, the settings editor's active zoom commit,
    // the Ctrl+Tab band restore, a fresh short project's second tab) can never
    // rest above the effective ceiling. The scattered tick/on_resize reclamps
    // that used to own this were the leaking surface; they now only trigger and
    // delegate here. Clamped BEFORE samples_visible below so `visible` reflects
    // the final level. clamp_zoom_level no-ops while loading. Parked (inactive-
    // tab) ViewState bands deliberately store zoom requests verbatim and are
    // governed here only once they go live -- at Ctrl+Tab restore or tab-in.
    a.zoom_level = clamp_zoom_level(a, audio, a.zoom_level);

    const int64_t visible = samples_visible(a, audio);
    const int64_t total   = live_total_frames(a, audio);
    if (visible >= total) {
        a.viewport_start_sample = 0;
        return;
    }
    if (a.viewport_start_sample < 0) a.viewport_start_sample = 0;

    // The rightmost on-grid rest, through the shared right-wall owner (the same
    // wall the strip drag's pan clamp uses). max_viewport_start_grid re-derives
    // visible/total/q and returns max(0, total-visible) when q is non-numeric.
    const int64_t max_start_grid = max_viewport_start_grid(a, audio);

    const double q = painter_samples_per_pixel(a, audio, waveform_area(a));
    if (q <= 0.0) {
        if (a.viewport_start_sample > max_start_grid)
            a.viewport_start_sample = max_start_grid;
        return;
    }

    // Snap the viewport to a whole-pixel (grid) boundary: every rest viewport is
    // then a true grid point, so the SOURCE-view single-rounding warp-marker commit
    // (authored_frame_at_column's source branch via source_grid_position_at_column)
    // lands EXACTLY on the frame-0 authoring grid, and the waveform rests pixel-
    // aligned. The same painter spp authored_frame_at_column uses, so viewport grid
    // and marker grid are one grid.
    //
    // Snap the viewport to its nearest grid point, then clamp into
    // [0, max_start_grid]. (Single clamp: do NOT also clamp to the off-grid
    // max_start first — that would pull a valid flush-right grid rest back
    // off-grid.)
    auto grid = [q](int64_t kk) {
        return static_cast<int64_t>(std::nearbyint(static_cast<double>(kk) * q));
    };
    int64_t snapped = grid(static_cast<int64_t>(
        std::nearbyint(static_cast<double>(a.viewport_start_sample) / q)));
    if (snapped < 0)               snapped = 0;
    if (snapped > max_start_grid)  snapped = max_start_grid;
    a.viewport_start_sample = snapped;
}

double playhead_pixel_x(const AppState& a, int64_t vp_start, double spp) {
    if (spp <= 0.0) return -1.0;
    return static_cast<double>(a.playhead_cursor_sample - vp_start) / spp;
}

double scanner_pixel_x(const AppState& a, int64_t vp_start, double spp) {
    if (spp <= 0.0) return -1.0;
    // Drive the scanner's pixel from the CONTINUOUS predictor position
    // (playhead_scanner_precise) rather than the quantized integer sample, so a
    // per-frame viewport rescale during a strip-drag zoom slides the scanner
    // smoothly instead of jittering on integer-frame steps. The line still
    // paints as a hard 1px column at the rounded pixel; only its basis is
    // continuous.
    return (a.playhead_scanner_precise - static_cast<double>(vp_start)) / spp;
}

// Shrink-and-pad: produce a union rectangle covering both inputs. Used to
// bundle the two playhead-column invalidations into a single expose when
// they overlap (e.g., arrow key at zoom level 0 moves by 1 pixel).
GuiRect union_rect(GuiRect a, GuiRect b) {
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.w, b.x + b.w);
    const int y1 = std::max(a.y + a.h, b.y + b.h);
    return GuiRect{x0, y0, x1 - x0, y1 - y0};
}

bool rects_intersect(GuiRect a, GuiRect b) {
    if (a.x + a.w <= b.x || b.x + b.w <= a.x) return false;
    if (a.y + a.h <= b.y || b.y + b.h <= a.y) return false;
    return true;
}

GuiRect playhead_invalidate_rect(const GuiRect& area, double px_x) {
    const int col = static_cast<int>(std::nearbyint(px_x));
    const int x0 = std::max(area.x, col - playhead_half_px());
    const int x1 = std::min(area.x + area.w, col + playhead_half_px() + 1);
    if (x1 <= x0) return GuiRect{area.x, 0, 0, 0};
    // Envelope extends up from the top of the window to the bottom of the
    // waveform area so it covers the playhead line inside the waveform AND
    // the triangle indicator in the flag strip above it.
    const int y0 = 0;
    const int y1 = area.y + area.h;
    return GuiRect{x0, y0, x1 - x0, y1 - y0};
}

// The status line and the transient/modal chain occupy the two rows of the
// bottom strip, so the invalidation region is the whole bottom strip rect (both
// rows repaint together).
GuiRect timestamp_invalidate_rect(const AppState& a) {
    return bottom_strip_area(a);
}


int main(int argc, char** argv) {
    if (!verify_c_numeric_locale("warptempo_gui")) return 1;

    // Auto-reap the fire-and-forget external audio players the `l`
    // ("Listen to renders") command spawns — the product's only subprocess.
    // Ignoring SIGCHLD makes the kernel discard child exit status so the
    // detached players never linger as zombies; set once here, never
    // per-press. No other code forks or waits, so nothing depends on the
    // default disposition.
    std::signal(SIGCHLD, SIG_IGN);

    // A source is loaded only from the command line: the GUI has no
    // in-session file open or drag-and-drop (open the next source by
    // relaunching). The audio path is therefore mandatory — there is no
    // blank-window state to load into.
    if (argc != 2) {
        std::fprintf(stderr, "usage: warptempo_gui <audio_file>\n");
        return 1;
    }
    const char* cli_path = argv[1];

    // The palette, before anything can paint or derive from a color. Missing
    // config is silent (compiled defaults); a malformed one prints one line and
    // keeps the compiled defaults. Nothing writes the palette after this point,
    // so every later reader — including the waveform worker thread — sees one
    // fixed set of colors for the process's life.
    load_color_config();

    AppState     app;
    GuiAudio     audio;
    GuiPlayback  playback;
    GuiPlatform  gui;
    WaveformCache wf_cache;
    // Top-strip flag rects live on their own surface, rebuilt
    // synchronously from on_tick. Constructed alongside wf_cache so they share
    // the same lifetime; passed by reference into GuiPaintHandler. (Trim has no
    // cache — every trim pixel paints live per frame in
    // GuiPaintHandler::paint_trim, below the playheads; the marker stem is the
    // selected-marker live overlay paint_selected_stem.)
    FlagCache     flag_cache;
    if (!gui.init(app.width, app.height, "warptempo_gui")) {
        return 1;
    }

    // -- Viewport + invalidation helpers ------------------------------------
    //
    // The viewport-mutation and invalidation helpers are methods on the
    // Viewport struct (viewport.{cpp,h}), including the two substantive hover
    // helpers clear_hover_popup and recompute_hover_at_cursor and the
    // timestamp invalidation helper invalidate_timestamp_area. Every other
    // cross-cutting operation is a method on its owning struct constructed
    // below — stop_playback_if_playing / toggle_playback / set_playback_speed
    // on playback_lifecycle, save on save_ops, request_close /
    // activate_response on prompt, refresh_active_tab_view_from_app on
    // active_views — reached by their callsites as direct method calls.

    Viewport viewport(app, audio, gui, playback);
    GuiPlaybackLifecycle playback_lifecycle(app, audio, playback, viewport);
    Selection selection(app, audio, viewport);
    GuiAsyncRenderer async_renderer;
    if (!async_renderer.init()) {
        std::fprintf(stderr,
            "warptempo_gui: failed to start async renderer; exiting\n");
        return 1;
    }
    // Waveform-cache rebuild runs on this dedicated worker; the
    // paint thread becomes blit-only. Must be constructed before
    // GuiPaintHandler (which takes it as a reference).
    GuiWaveformWorker waveform_worker;
    if (!waveform_worker.init()) {
        std::fprintf(stderr,
            "warptempo_gui: failed to start waveform worker; exiting\n");
        return 1;
    }
    // Shared process-local render cache for target-view reuse, archival
    // reuse/publish rungs, and committed-render survival after the renders
    // folder is wiped. init() creates the per-process cache directory under
    // the user cache home and sweeps dead-PID orphan directories; shutdown(),
    // after the event loop, removes this process's directory. Constructed
    // before target_render, which holds it by reference. A failed init() leaves
    // the cache disabled (every lookup misses), so target_render needs no
    // special-casing.
    RenderCache render_cache;
    render_cache.init();
    // GuiTargetRender is the cancel-restart dispatcher for target-view
    // live audio. It must be constructed after async_renderer
    // (a dependency) and BEFORE the op clusters (which take it as a
    // ref). The trigger() method is a no-op in source view, so injecting
    // it into source-view-only call sites is harmless.
    GuiTargetRender target_render(app, audio, async_renderer, playback,
                                  viewport, render_cache);
    // Paint handler constructed before file_loader, which applies font_size
    // changes through its on_resize (the shared geometry-and-cache rebuild
    // path). The settings-editor font_size commit uses the input handler's own
    // paint_handler ref for the same rebuild.
    GuiPaintHandler paint_handler(app, audio, playback, wf_cache,
                                  flag_cache, waveform_worker, gui);
    // file_loader holds a GuiTargetRender& (its end-of-load ensure_ready()
    // dispatches the eager target preview), so it must be constructed after
    // target_render.
    GuiFileLoader file_loader(app, audio, gui, playback, viewport,
                              target_render, paint_handler, selection);
    GuiActiveViews active_views(app, audio, viewport, selection,
                                playback_lifecycle);
    Undo undo(app, viewport, selection, playback_lifecycle, active_views,
              target_render);
    GuiPhaseResetMarkersOps phase_resets(app, audio, viewport, selection, undo,
                                         playback_lifecycle, target_render);
    GuiWarpMarkersOps warpops(app, audio, viewport, selection, undo,
                              playback_lifecycle, target_render);
    MarkerDragOps marker_drag(app, audio, viewport, selection, undo,
                              target_render);
    GuiFlagEditor flag_editor(app, audio, viewport, selection, undo,
                              target_render);
    GuiRendersDir renders_dir(app);
    PhaseResetPropagate phase_reset_propagate(app, viewport, undo,
                                              target_render, active_views,
                                              playback_lifecycle);
    GuiSaveOps save_ops(app, undo, active_views, viewport);
    GuiPrompt prompt(app, gui, viewport,
                     phase_reset_propagate, save_ops, playback_lifecycle);
    GuiSettingsEditor settings_editor(app, audio, viewport, selection,
                                      active_views, undo,
                                      target_render, playback_lifecycle);
    gui.set_worker_completion_fd(async_renderer.completion_fd(),
        [&async_renderer]() { async_renderer.on_completion_event(); });
    gui.set_waveform_worker_completion_fd(waveform_worker.completion_fd(),
        [&waveform_worker]() { waveform_worker.on_completion_event(); });
    GuiInputHandler input_handler(app, audio, gui, playback,
                                  viewport, selection, undo,
                                  warpops, phase_resets, marker_drag,
                                  flag_editor,
                                  renders_dir, active_views,
                                  phase_reset_propagate,
                                  async_renderer,
                                  playback_lifecycle, save_ops, prompt,
                                  settings_editor, target_render,
                                  paint_handler);
    // Back-wire the settings editor to the input handler (constructed after the
    // editor, which the input handler holds by reference — the cycle is
    // resolved with a pointer set here). The editor reaches
    // handle_active_audio_view_toggle / apply_font_size / auto_clear_crossed_trim
    // through it, so a `:`-typed GUI key funnels into the same gesture code.
    settings_editor.input = &input_handler;
    // Same back-wire for the phase-reset propagate: its paste tail lands in
    // target view through handle_active_audio_view_toggle, the chokepoint that
    // lives on the input handler (constructed after the propagate, which the
    // input handler holds by reference — the cycle is resolved with this
    // pointer set).
    phase_reset_propagate.input = &input_handler;

    // Viewport worker kick: FOLLOW-SCROLL during playback is the one caller
    // that requests the new waveform immediately rather than waiting for the
    // next tick (Viewport::follow_scroll_if_needed, the sole
    // kick_waveform_render call site outside kick_waveform_sync's
    // callback-unwired fallback). The worker's OTHER undriven work — a
    // compositor resize and the launch load, both of which run on_resize, which
    // only stores the new dimensions, re-clamps zoom/viewport, and re-anchors
    // the playback predictor if the level moved: no enqueue and no cache
    // rebuild there — is discovered instead when
    // maybe_enqueue_waveform_render's dirty-detect sees the changed fingerprint
    // on the next tick, which is also where the cache rebuild happens. (Pan, zoom, center and the one-shot jumps do NOT come here at
    // all: they are user-driven and render synchronously through
    // request_waveform_sync_ below.) maybe_enqueue_waveform_render is
    // main-thread-safe and idempotent
    // against the on_tick backstop, so the earlier trigger only shortens
    // input-to-render latency. See Viewport::kick_waveform_render.
    viewport.request_waveform_render_ =
        [&]() { paint_handler.maybe_enqueue_waveform_render(); };

    // (No pan callback: scroll_viewport routes through request_waveform_sync_
    // below, like zoom. The incremental shift-and-strip pan was retired
    // 2026-07-26 — one render path for moving and resting plates.)

    // Every user-driven viewport change routes here instead of the async
    // worker — zoom, scroll/pan, and the one-shot jumps alike: render
    // the plate synchronously and publish the displayed fingerprint now so the
    // overlays and waveform land in the same frame. See
    // Viewport::kick_waveform_sync and
    // GuiPaintHandler::force_synchronous_waveform_rebuild.
    viewport.request_waveform_sync_ =
        [&]() { paint_handler.force_synchronous_waveform_rebuild(); };

    // Pointer capture: the input handler's begin/end hooks drive the platform's
    // cursor lock (pointer-constraints + relative-pointer). Shared by two
    // waveform gestures — the ctrl-exact strip drag and the alt-exact pan — for
    // infinite pan/zoom travel.
    // Both platform methods self-guard (begin no-ops when a capture is live or
    // the compositor lacks the managers; end is idempotent), so the input layer
    // stays agnostic to whether capture is available.
    input_handler.begin_strip_pointer_capture = [&]() { gui.begin_pointer_capture(); };
    input_handler.end_strip_pointer_capture   = [&]() { gui.end_pointer_capture(); };
    input_handler.set_strip_capture_restore_x = [&](double sx) { gui.set_capture_restore_x(sx); };

    // Displayed-map promotion → same-frame hover refresh. on_redraw fires this
    // the instant it advances displayed_map_gen (before any painting), so the
    // hover run/readout this promoting frame paints — and any Ctrl+C before the
    // next tick — resolve against the just-promoted map. The on_tick displayed_gen
    // check stays as the backstop for promotion-free store mutations.
    paint_handler.on_displayed_map_promoted =
        [&]() { viewport.recompute_hover_at_cursor(); };

    auto invalidate_timestamp_area   = [&]() { viewport.invalidate_timestamp_area(); };
    auto invalidate_playhead_columns = [&](double a, double b) { viewport.invalidate_playhead_columns(a, b); };
    auto follow_scroll_if_needed     = [&]() { viewport.follow_scroll_if_needed(); };

    std::string pending_initial_load = cli_path;
    bool        initial_load_done    = false;

    // -- Redraw -------------------------------------------------------------

    gui.set_on_redraw([&](cairo_t* cr, int x, int y, int w, int h) {
        paint_handler.on_redraw(cr, x, y, w, h);
    });

    gui.set_on_resize([&](int w, int h) {
        // A compositor configure can change zoom, samples-per-pixel, and the
        // viewport beneath an in-flight positional drag whose grab anchor is a
        // frame coordinate computed from the old geometry, so the next motion
        // event would derive its delta across two different coordinate systems
        // and commit a spurious jump. END any in-flight pointer gesture before
        // on_resize applies, so the resize always lands on a gesture-free state.
        // ENDING IS COMMITTING (architect 2026-07-29 — pointer gestures have no
        // cancel; the rule is at the drag-modal gate in input_handler.cpp): each
        // gesture runs its own release body, so a resize mid-drag keeps what the
        // last motion event proposed — computed against the OLD geometry, which was
        // valid when it was proposed; it is the NEXT motion that could not be
        // trusted. finalize_active_drags is a no-op when nothing is live, so the
        // plain no-gesture resize path is unaffected. A live editor text-
        // selection drag is finalized there too (collapsed to a caret,
        // selection-only, nothing to revert).
        input_handler.finalize_active_drags();
        paint_handler.on_resize(w, h);
    });

    auto invalidate_top_strip     = [&]() { viewport.invalidate_top_strip(); };

    // popup_eligible_marker is a free function in app_state.{h,cpp}. Its
    // callers in this TU (Viewport::recompute_hover_at_cursor, on_tick) reach
    // it directly with the (app, idx) signature; on_motion calls it from
    // input_handler.cpp. The hover-popup and iteration-mode comments live
    // above the declaration in app_state.h.

    // The drag and selection-shift operations are methods on the
    // GuiWarpMarkersOps struct (warpmarkers_ops.{cpp,h}).

    // The shared wheel handler (handle_wheel) is a private helper method on
    // GuiInputHandler; GuiInputHandler::on_wheel is its only caller.

    // The batch render runner (start_render_batch and the ActiveBatch
    // lifecycle) is a set of private helper methods on GuiInputHandler
    // (input_handler.h), driven by the iteration and BPM sweeps.

    gui.set_on_key([&](GuiKey key, GuiInputState mods) {
        input_handler.on_key(key, mods);
    });

    gui.set_on_close([&]() {
        // Window-manager close (title-bar X) routes through the unsaved-
        // work dialog when dirty, same as Ctrl+Q. END any in-flight pointer
        // gesture before the prompt goes up, matching the Ctrl+Q
        // hatch: while the prompt is up the pointer handlers swallow motion
        // and release, so a gesture left alive would commit on the next motion
        // if the user dismisses the prompt. ENDING IS COMMITTING (pointer
        // gestures have no cancel — the rule is at the drag-modal gate in
        // input_handler.cpp): each gesture runs its own release body here.
        // finalize_active_drags is a no-op when nothing is live, so the clean and
        // non-gesture close paths are unaffected. A live editor text-selection
        // drag is finalized there too (collapsed to a caret, selection-only,
        // nothing to revert), so there is no motion-free interval where it would
        // swallow keys until a later pointer motion noticed the lost button.
        input_handler.finalize_active_drags();
        prompt.request_close();
    });

    gui.set_on_button_press([&](GuiMouseButton button, int x, int y,
                                GuiInputState mods) {
        input_handler.on_button_press(button, x, y, mods);
    });

    gui.set_on_button_release([&](GuiMouseButton button, int x, int y,
                                  GuiInputState mods) {
        input_handler.on_button_release(button, x, y, mods);
    });

    // Scroll wheel arrives coalesced once per pointer frame, carrying the
    // net detent count, so the per-step wheel machinery runs once regardless
    // of how many detents a fast touchpad burst crossed in that frame.
    gui.set_on_wheel([&](GuiMouseButton dir, int steps, int x, int y,
                         GuiInputState mods) {
        input_handler.on_wheel(dir, steps, x, y, mods);
    });

    // The same wheel routing predicate on_wheel gates with, exposed to the
    // platform so its per-frame accumulator binds sub-detent remainder to the
    // context it will emit in — one predicate, two surfaces that never drift.
    gui.set_wheel_context_probe([&](int x, int y) {
        return input_handler.wheel_context(x, y);
    });

    // kLeftClickKey emulates its platform-boundary form (the left mouse
    // button), except while a text editor is open, when it stays a normal
    // letter. This probe is the "editor open" truth the platform consults at
    // its press time.
    gui.set_text_editor_active_probe([&]() {
        return input_handler.any_text_editor_active();
    });

    // Press-time key-repeat eligibility: the platform consults this before
    // arming repeat so one-shot commands and editor openers never repeat while
    // held-step gestures and editor typing do.
    gui.set_repeat_eligible_probe([&](GuiKey key, GuiInputState mods) {
        return input_handler.repeat_eligible(key, mods);
    });

    // Pointer-leave / capability-loss hover drop: no motion event follows those
    // edges, so clear_hover_popup here erases the hover POPUP / marker-text run /
    // readout when the pointer slides out the window edge. It is NOT a
    // stem-visibility arm — the selected-marker stem is always-on for a singleton
    // and the hover clear neither changes stem visibility nor issues any
    // dedicated stem-column damage (its top-strip invalidation may
    // incidentally repaint the one-row waveform seam, which redraws the same stem,
    // never hides it); re-entry's synthesized motion re-resolves the hover surfaces.
    // The redesigned rows' button hover faces ride the same edge for the same
    // reason (no motion event follows a leave, so a pointer that slides out of
    // the window over a button would leave its pill / outline lit); that is
    // separate state from the marker hover, so it takes its own clear. Both are
    // transition-gated.
    gui.set_pointer_left_hook([&] {
        viewport.clear_hover_popup();
        input_handler.clear_redesign_button_hover();
    });

    gui.set_on_motion([&](int mouse_x, int mouse_y, GuiInputState mods) {
        input_handler.on_motion(mouse_x, mouse_y, mods);
    });

    // -- File loading --------------------------------------------------------
    //
    // load_file (file_loader.{h,cpp}) is the sole loader, invoked once from
    // the startup tick below. The GUI has no in-session file open or
    // drag-and-drop: the source is fixed at launch, so there is nothing to
    // wire here.

    // Tick: runs once per event-loop iteration. During playback, snapshots
    // the audio thread's cursor and mirrors it into the main-thread playhead,
    // invalidating just the columns and timestamp that changed. Also
    // detects natural end-of-playback via the atomic playing flag.
    gui.set_on_tick([&]() {
        // Startup file load, deferred out of pre-run() so the window maps and
        // paints first (the compositor's initial configure / first frame only
        // land once run() is pumping). Gated on has_initial_configure() so the
        // load — and its loading notice — run against a mapped, painted surface.
        if (!initial_load_done && !pending_initial_load.empty() &&
            gui.has_initial_configure()) {
            initial_load_done = true;
            const std::string p = std::move(pending_initial_load);
            pending_initial_load.clear();
            if (!file_loader.load_file(p)) {
                // The source argument is mandatory and there is no in-session
                // replacement surface, so every load refusal is terminal.
                // Deeper decode/sidecar failures already request exit at the
                // owning site; request_exit() is idempotent for those paths.
                gui.request_exit();
                return;
            }
            // Render-environment check, after the sidecars applied
            // successfully: compare the four STORED hashes against the
            // running environment's and open the advisory mismatch prompt
            // naming the changed libraries. Detection, not prevention — a
            // mismatch render stays fully valid; 'o', the sole response,
            // restamps (history-less, no-dirty GUI-kind state; the next
            // ordinary Ctrl+S persists it), and no dismiss-without-ack path
            // exists.
            {
                const RenderEnvHashes& cur = compute_render_env_hashes();
                std::string changed;
                auto note = [&changed](const char* name) {
                    if (!changed.empty()) changed += ", ";
                    changed += name;
                };
                if (app.libm_hash          != cur.libm)          note("libm");
                if (app.libmvec_hash       != cur.libmvec)       note("libmvec");
                if (app.fftw3_hash         != cur.fftw3)         note("fftw3");
                if (app.fftw3_threads_hash != cur.fftw3_threads) note("fftw3_threads");
                if (!changed.empty()) prompt.open_env_hash_mismatch(changed);
            }
            return;  // loaded state paints on the next tick
        }

        // Dirty-detect for the waveform cache. Compares the
        // current desired fingerprint against pending_fp_* and either
        // dispatches to the worker, sets the supersede slot, or no-ops.
        // Runs first so the worker is kicked off before any of the
        // tick-time paint invalidations below.
        paint_handler.maybe_enqueue_waveform_render();

        // Backstop: if the live-domain total changed under the current view
        // the current zoom level and viewport may sit outside the new bounds.
        // Re-clamp both; when either actually moved, the displayed geometry
        // changed discretely, so rebuild synchronously (same class as
        // drop_marker — see warpmarkers_ops.cpp). Synchronous edit tails (scale
        // commit, tempo edit, marker move in target view) now pre-clamp through
        // kick_waveform_sync and render final geometry in one shot, so this
        // backstop finds their geometry already clamped (no movement, no second
        // render). live_total_frames is live-warp-map-derived, and every path
        // that can change that map is now synchronous (edits) or self-clamping
        // (load/adopt), so no NAMED asynchronous case remains — preview
        // completion repaints the plate but never touches the live map. This
        // stays cheap belt-and-braces insurance (a silent-wrong-geometry guard)
        // for any future path that moves the total without clamping.
        {
            const int64_t lt = live_total_frames(app, audio);
            if (app.last_tick_live_total != lt) {
                app.last_tick_live_total = lt;
                // The level ceiling and the viewport clamp both live in
                // clamp_viewport_start now; the tick keeps only its TRIGGER role
                // (a live-total change must repair geometry without user input)
                // and detects movement for the discrete-rebuild side effects.
                const double  old_zoom = app.zoom_level;
                const int64_t old_vp   = app.viewport_start_sample;
                clamp_viewport_start(app, audio);
                // Mirror kick_waveform_sync's repair: a total
                // change can strand the resting playhead or a live region
                // outside the new domain even when the geometry itself did not
                // move, so repair unconditionally within the total-changed
                // block, not behind the geometry-moved gate. Idempotent, so the
                // second call inside kick_waveform_sync below (moved case) is a
                // no-op.
                viewport.clamp_display_state_to_live_domain();
                if (app.zoom_level != old_zoom ||
                    app.viewport_start_sample != old_vp) {
                    viewport.invalidate_waveform_area();
                    viewport.invalidate_timestamp_area();
                    viewport.kick_waveform_sync();
                }
            }
        }

        // Flag-rect cache dirty-detect. Runs AFTER the waveform's
        // dirty-detect on purpose — both layers key their displayed-
        // viewport inputs off wf_cache.fp_*, so on a viewport-change
        // tick the waveform enqueues and the flags hold the OLD
        // viewport; on a post-swap tick (eventfd handler runs before
        // on_tick) wf_cache.fp_* already carries the new viewport, so
        // flags snap together with the just-blitted waveform.
        paint_handler.maybe_rebuild_flag_cache();

        // Stationary-cursor hover refresh (the BACKSTOP). A keyboard mutation
        // (tempo step, Ctrl+N, nudge) changes the hovered marker's fields/position
        // without a pointer-motion event, so nothing else re-reads the hover text.
        // A silent displayed-map promotion also moves the flag under the
        // stationary cursor, but the promoting frame now refreshes hover itself
        // (paint_handler.on_displayed_map_promoted, fired inside on_redraw before
        // painting) — so the displayed_gen arm here only catches a promotion whose
        // frame does not run this recompute (and re-stamps displayed_gen so the two
        // routes settle together after one pass). When a hover is showing and
        // either store's generation OR the displayed-map generation moved past what
        // the hover cached, drive one recompute here — the shared per-frame route,
        // not a per-mutation-site call — so the lane/readout refresh in the same
        // frame the change paints. recompute_hover_at_cursor re-stamps all three,
        // so this settles after one pass; a cleared hover (marker_index < 0) never
        // trips it.
        if (app.hover_popup.marker_index >= 0 &&
            (app.hover_popup.warp_gen  != app.warpmarkers.generation() ||
             app.hover_popup.phase_gen != app.phaseresetmarkers.generation() ||
             app.hover_popup.displayed_gen != app.displayed_map_gen)) {
            viewport.recompute_hover_at_cursor();
        }

        // Blink the editor cursor independently of playback. Compare the
        // current visibility against the last painted state and invalidate
        // the top strip when it flips. Cheap: top_strip is small.
        if (text_editor::is_active(app.top_flag_editor)) {
            const bool now_visible =
                text_editor::cursor_visible_now(app.top_flag_editor);
            if (now_visible != app.top_flag_editor_blink_last) {
                app.top_flag_editor_blink_last = now_visible;
                if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket)
                    invalidate_timestamp_area();
                else
                    invalidate_top_strip();
            }
        }
        // Same shape for the bottom-strip settings prompt; invalidate the
        // timestamp area on each visibility flip.
        if (text_editor::is_active(app.settings_editor)) {
            const bool now_visible =
                text_editor::cursor_visible_now(app.settings_editor);
            if (now_visible != app.settings_editor_blink_last) {
                app.settings_editor_blink_last = now_visible;
                invalidate_timestamp_area();
            }
        }
        // Same shape for the bottom-strip render-commit prompt.
        if (text_editor::is_active(app.commit_editor)) {
            const bool now_visible =
                text_editor::cursor_visible_now(app.commit_editor);
            if (now_visible != app.commit_editor_blink_last) {
                app.commit_editor_blink_last = now_visible;
                invalidate_timestamp_area();
            }
        }

        if (app.loading || audio.total_frames() <= 0) return;

        const bool ma_playing = playback.is_playing();
        if (!app.playhead_scanner_active && !ma_playing) return;

        if (ma_playing) {
            // Heartbeat: invalidate the scanner column at the current
            // model position so the paint cycle keeps running. The
            // pre-paint hook reads the predictor at paint time and adds
            // damage for the actually-painted position. We do not read
            // the predictor or update app.playhead_scanner_sample here
            // — the pre-paint hook owns that work, keeping predictor
            // sampling on the paint clock; reading it on the tick too
            // would reintroduce the tick/paint sampling-rate mismatch
            // that stutters playhead motion at high zoom. While the
            // scanner column is onscreen the timestamp area is
            // invalidated only by the pre-paint hook (when the
            // predictor advances past app.playhead_scanner_sample),
            // never by the tick — the tick fires ~2x per frame, so
            // duplicating the timestamp rect there is wasted on_redraw
            // work.
            // PLATE basis, not live: the scanner's pixels are plate-registered
            // (paint_playheads), so its damage resolves there too — the rule and
            // the per-site shape table live at playhead_pixel_x, app_state.h.
            // NARROW is mandatory here (this fires ~2x per frame while playing),
            // and this lambda's scope reaches paint_handler, so the site takes
            // the narrow-on-plate shape rather than widening.
            const GuiPaintHandler::PlateViewportBasis pb =
                paint_handler.plate_viewport_basis();
            const double px = scanner_pixel_x(
                app, static_cast<int64_t>(pb.vp_start), pb.spp);
            invalidate_playhead_columns(px, px);
            // OFFSCREEN FALLBACK — the paint clock has to keep running
            // even when the scanner column contributes nothing.
            // playhead_invalidate_rect yields a zero-width rect for a
            // column outside the waveform area and
            // invalidate_playhead_columns emits no damage for one, so a
            // scanner that has left the viewport (a launch inside a trim
            // window the user then panned away from, follow off)
            // leaves this tick producing NO damage at all — and with the
            // pre-paint hook the sole advancer of the scanner position
            // while playing, no damage means no paint means the position
            // can never come back. So ask the rect builder rather than
            // re-derive its bounds test here (the tick damages one
            // column, so that single rect decides it), and when it yields
            // nothing, damage the bottom strip instead: it is always
            // onscreen, so a paint is always produced, and it is the
            // honest rect — the status line tracks the SCANNER's time
            // while playing, so it is frozen alongside the line for
            // exactly the same stretch. Follow mode is untouched: no
            // viewport work is added here, and a chasing viewport keeps
            // the column onscreen so the fallback simply never fires.
            if (playhead_invalidate_rect(waveform_area(app), px).w <= 0)
                invalidate_timestamp_area();
            return;
        }

        // Playing was true last tick, now false — natural end. Deactivate the
        // scanner THIS tick, through the same call Space's stop edge makes:
        // a stopped scanner is deactivated immediately and there is no
        // non-playing window in which its value fields are valid (contract at
        // app_state.h's scanner block). The scanner's last-painted pixels are
        // damaged by the call, so the line vanishes from wherever the predictor
        // last drew it — a few pixels short of the exclusive end bound, the
        // accepted delta.
        // A NATURAL END IS EXACTLY A SPACE STOP (architect 2026-07-29, "the
        // simplest symmetry"), and since 2026-07-30 that is literally ONE CALL: the
        // hand-spelled pair this branch and Space's stop edge both carried
        // collapsed onto stop_playback_if_playing, the product's one stop body. The
        // follow-scroll tail that used to run here — `if (follow_mode &&
        // !follow_overridden_for_session) follow_scroll_if_needed();` — is DELETED,
        // and the two asymmetries flagged against it (its override guard was dead,
        // the stop having just cleared that flag; and it could scroll the viewport
        // BACK to the restored playhead, which the Space stop never does) die WITH
        // the arm rather than being fixed inside it.
        //
        // THE FENCE-BEFORE-FLAG-CLEAR ORDERING SURVIVES THE COLLAPSE, and it is the
        // whole reason this branch calls anything at all: `playing` is already false
        // here — the JACK process callback published it (release store) at the
        // natural end — but `GuiPlayback::stop()` is the QUIESCENCE FENCE, not a flag
        // write, and the helper takes it FIRST and only then clears
        // `playhead_scanner_active`, exactly as the pair did. The callback that
        // published false has NOT necessarily retired: the fence waits for
        // `process_cycles` to advance by two, which is what proves a full callback
        // ran start-to-finish afterward and orders all of its buffer reads before
        // anything the main thread mutates next. `rebind_buffer` REQUIRES that fence
        // by contract (playback.cpp). Without it the tick cleared
        // `playhead_scanner_active` while the fence was still untaken, and that left
        // exactly one reachable bypass: a post-natural-end S/T switch, whose
        // `stop_playback_if_playing` early-returns once BOTH flags are false, after
        // which `ensure_ready` / `rebind_to_source` skip their own
        // `is_playing()`-gated stop and rebind UNFENCED. Taking the fence here, at
        // the stop edge, closes it at the source — the Space-symmetric design, and
        // the reason the rebind sites keep their conditional stops rather than
        // becoming unconditional. Cost: the fence blocks the tick for up to ~2 JACK
        // periods, once per playback session, which is precisely what Space's stop
        // edge has always paid. stop() is idempotent on an already-flag-stopped
        // session — it re-stores the same false and then just waits.
        //
        // NO OUTER `if (app.playhead_scanner_active)` IS NEEDED: reaching this line
        // means the early return above did not fire and `ma_playing` is false, so the
        // scanner flag is true — and that is bit-for-bit the helper's own entry guard
        // (`!is_playing() && !scanner_active`), which now carries it.
        playback_lifecycle.stop_playback_if_playing();
    });

    gui.set_on_pre_paint([&]() {
        if (app.loading || audio.total_frames() <= 0) return;
        if (!playback.is_playing()) return;

        // (The loop-wrap predictor resync that stood here is GONE with all
        // audition looping, architect 2026-07-30: the read position only ever
        // advances now, so there is no backward cursor jump for the free-running
        // predictor to miss and no wrap counter to poll. Every session plays its
        // window once and takes the natural-end teardown.)

        // Read the predictor at paint time. The predictor is continuous
        // in wall time, so this gives the freshest possible position
        // right before paint consumes the damage list. Under the
        // split-playhead model the predictor advances the scanner only
        // — the cursor stays where the user left it.
        //
        // playback.cursor() reports the bound buffer's own domain: the
        // domain offset travels with the bind (playback.h), so this is
        // full-target-frame when the target buffer is bound and the source
        // frame (offset 0) when source.wav is bound —
        // exactly app.playhead_scanner_sample's domain, no translation here. A
        // paint racing a target dispatch reads whatever buffer is
        // actually bound, with that buffer's own offset; a stale bias
        // can never be applied to the wrong buffer's cursor, structurally.
        // (An old app-side bias field needed a target_buffer_frames > 0
        // guard here for exactly that skew.)
        const int64_t cur = playback.cursor();
        // The integer cursor is the change-detection anchor: gate on it so a
        // sub-frame advance that has not yet crossed an integer boundary adds no
        // damage here (the stationary no-repaint case). A viewport change that
        // moves the continuous pixel without changing cur is covered elsewhere —
        // the strip-drag path invalidates the whole waveform area each frame, so
        // the scanner repaints at its new continuous pixel through the normal
        // paint even while this gate holds.
        if (cur == app.playhead_scanner_sample) return;

        // PLATE basis for both columns, not live: the scanner's pixels are
        // plate-registered (paint_playheads), so its damage resolves there —
        // the rule and the per-site shape table live at playhead_pixel_x
        // (app_state.h). NARROW is mandatory here (this is the per-frame
        // scanner advance) and this lambda reaches paint_handler, so the site
        // takes the narrow-on-plate shape rather than widening.
        const GuiPaintHandler::PlateViewportBasis pb =
            paint_handler.plate_viewport_basis();
        const int64_t pb_vp = static_cast<int64_t>(pb.vp_start);
        // OLD COLUMN, DERIVED — never stashed (the viewport-mutation stash
        // playhead_scanner_old_px_stash was retired 2026-07-30; do not reintroduce
        // one). The plate basis IS the answer by construction: paint_playheads
        // draws the scanner through this same basis, so scanner_pixel_x against it,
        // read with the still-current playhead_scanner_precise (the last painted
        // continuous position), names exactly the column the last paint put the
        // line at. A stash could only be WORSE — its producers captured on the LIVE
        // viewport, which parts company with the plate during every async publish
        // window (follow scroll, resize, load, preview-driven total drift), and two
        // of them published on paths that then discovered they had moved nothing.
        // Nor is a stash needed: every viewport mutator that could reflow the
        // scanner's column pairs its write with full waveform-area damage AND a
        // synchronous plate rebuild (kick_waveform_sync, which damages y=0 through
        // the waveform's bottom itself), so the old line is erased wholesale in the
        // mutating frame and this narrow pair only ever has to cover the ordinary
        // per-frame advance.
        const double old_px = scanner_pixel_x(app, pb_vp, pb.spp);
        // Advance both fields: the integer sample (domain / change-detection)
        // and the continuous position the scanner pixel is drawn from. new_px
        // and the invalidated span are therefore computed from the continuous
        // pixel, matching what paint draws.
        app.playhead_scanner_sample = cur;
        app.playhead_scanner_precise = playback.cursor_precise();
        const double new_px  = scanner_pixel_x(app, pb_vp, pb.spp);

        // invalidate_region during pre-paint appends to damage_ without
        // scheduling a redundant frame callback (platform layer handles
        // that via its in_pre_paint_ flag).
        invalidate_playhead_columns(old_px, new_px);
        invalidate_timestamp_area();
        if (app.follow_mode && !app.follow_overridden_for_session)
            follow_scroll_if_needed();
    });

    // Paint the initial background before any synchronous load begins so the
    // window isn't briefly blank on fast disks.
    gui.invalidate_region(0, 0, app.width, app.height);
    gui.drain_events();

    gui.run();
    // Tear the audio device down before the sample buffer goes out of scope.
    playback.shutdown();
    gui.shutdown();
    // Join the render worker before cache teardown so a render completing
    // during shutdown cannot touch the dismantled cache. Idempotent; the
    // destructor's later call is then a no-op.
    async_renderer.shutdown();
    // Remove this process's render-cache directory.
    render_cache.shutdown();
    return 0;
}
