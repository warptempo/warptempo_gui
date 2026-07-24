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
// monospace_row_h(), flag_lane_h_px(), playhead_triangle_h_px(), kRowGapPx, and
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
// non-static: its explicit-domain form is called from input_render_dispatch.cpp
// (the queue/dispatch view-anchor math), so it cannot be main-private.
//
// Per-strip lane stacks with per-lane heights (the former uniform-row contract
// is superseded). Top and bottom strips now DIFFER in height; the waveform
// flexes between them. The TOP strip is FIVE lanes (from the window edge
// inward): zoom row (monospace_row_h()), trim-chip row (the flag height),
// marker-text row (monospace_row_h()), flag row (the flag height), and the
// triangle row (the triangle height) flush on the waveform. The BOTTOM strip is
// TWO lanes: the status row (outer) and the editor/modal row (inner). The lanes
// pack tight — the inter-lane gaps kRowGapPx and the outer/waveform-side gaps
// kFlagBottomLiftPx are all 0 — but the derivation below keeps them explicit so
// they reappear structurally if ever un-zeroed. ONE shared helper —
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
// Per-lane pixel heights, indexed from each strip's window edge inward. The top
// strip's flag and trim-chip rows carry the flag height; its zoom and
// marker-text rows carry the monospace row height; its innermost lane carries
// the triangle height. Both bottom lanes are monospace rows.
constexpr int kTopLaneCount    = 5;
constexpr int kBottomLaneCount = 2;
int top_lane_height(int lane) {
    switch (lane) {
        case 0: return monospace_row_h();        // zoom row
        case 1: return flag_lane_h_px();         // trim-chip row
        case 2: return monospace_row_h();        // marker-text row
        case 3: return flag_lane_h_px();         // flag row
        case 4: return playhead_triangle_h_px(); // triangle row (flush on waveform)
        default: return 0;
    }
}
int bottom_lane_height(int /*lane*/) {
    return monospace_row_h();                    // status row, editor/modal row
}
int strip_total_h(bool top_strip) {
    int sum = 2 * static_cast<int>(kFlagBottomLiftPx);  // outer + waveform-side gaps
    const int lanes = top_strip ? kTopLaneCount : kBottomLaneCount;
    for (int i = 0; i < lanes; ++i)
        sum += top_strip ? top_lane_height(i) : bottom_lane_height(i);
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
// Paint/hit agreement invariant: the trim-chip row is TOP lane 1, and
// hit_test_trim_chip / the pair-drag y-gate read top_upper_row_area(app), the
// exact band render_trim_flags paints the b/e chips in. Because both derive
// that band from this one helper, paint and hit cannot drift.
GuiRect strip_row_rect(const AppState& a, bool top_strip,
                       int lane_from_window_edge) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    int inset = static_cast<int>(kFlagBottomLiftPx);
    for (int i = 0; i < lane_from_window_edge; ++i) {
        inset += top_strip ? top_lane_height(i) : bottom_lane_height(i);
        inset += static_cast<int>(kRowGapPx);
    }
    const int lane_h = top_strip ? top_lane_height(lane_from_window_edge)
                                 : bottom_lane_height(lane_from_window_edge);
    const int y = top_strip ? inset : (h - inset - lane_h);
    return GuiRect{0, y, w, lane_h};
}

// Top strip lanes, counted down from the window top (index 0 = the window edge).
// Lane 0 is the zoom-strip row (a live drag surface painted as an empty ring, at
// the window edge); lane 1 is the b/e trim-chip row; lane 2 is the marker-text
// row (hosts the hover popup and the flag editor's live text, one at a time —
// paint_marker_text_lane); lane 3 is the
// flag row (the marker flag rectangles); lane 4 is the triangle row, whose
// bottom edge is flush with the waveform area top and which holds the flags' and
// the playhead's triangles.
GuiRect top_zoom_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 0);
}

GuiRect top_upper_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 1);
}

GuiRect top_marker_text_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 2);
}

GuiRect top_flag_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 3);
}

GuiRect top_triangle_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 4);
}

// Bottom strip lanes, counted up from the window bottom (index 0 = the window
// edge). bottom_lower_row (outer, index 0) carries the always-on status line;
// bottom_upper_row (inner, index 1) carries the modal/editor/queue chain and,
// below it, the pass/ref resolved hover readout (a marker's own value shows in
// the top strip's marker-text lane).
// (The former pan-strip row retired — pan lives on the Alt+drag waveform grab
// and the zoom strip's horizontal drag axis.)
GuiRect bottom_upper_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/false, 1);
}

GuiRect bottom_lower_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/false, 0);
}

// Resolve the trim playback/navigation range from AppState's trim fields.
// An unset bound (has_begin/has_end false) falls back to its side's extreme
// of [0, total_frames]. The stored bounds are
// already whole int64 frames; each side clamps to
// [0, total_frames] independently so playback ranges stay inside the
// buffer. There is NO
// ordering clamp: the pair passes through as authored. Crossed/equal
// bounds can no longer REST (the commit and load auto-clears destroy such
// pairs), but MID-GESTURE crossing stays free and this runs per frame, so
// consumers (the Space gate's cursor-in-[begin,end) check, Home/End via
// trim_range, the load-time playhead) still degrade to a no-op or a
// per-side position and must not assume begin <= end.
std::pair<long long, long long> compute_trim_samples(
    const AppState& a, long long total_frames) {
    long long begin = 0;
    long long end   = total_frames;

    if (a.trim.has_begin) {
        begin = a.trim.begin_frame;
    }
    if (a.trim.has_end) {
        end = a.trim.end_frame;
    }
    if (begin < 0) begin = 0;
    if (begin > total_frames) begin = total_frames;
    if (end < 0) end = 0;
    if (end > total_frames) end = total_frames;
    return {begin, end};
}

double samples_per_pixel_at(double zoom_level,
                            int waveform_width_px,
                            int64_t total_frames,
                            int sample_rate) {
    // One continuous domain, no sentinel: ms_per_px = 0.625 * 2^(level - 1).
    // total_frames / waveform_width_px play no part — at the per-file effective
    // ceiling the exponent already yields spp = total/width (whole song
    // visible) by construction, so there is no fit-file special case.
    (void)waveform_width_px;
    (void)total_frames;
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
    const double spp = samples_per_pixel_at(
        a.zoom_level, area.w, live_total_frames(a, audio), audio.sample_rate());
    return static_cast<int64_t>(std::nearbyint(spp * area.w));
}

double current_samples_per_pixel(const AppState& a, const GuiAudio& audio) {
    const GuiRect area = waveform_area(a);
    return samples_per_pixel_at(
        a.zoom_level, area.w, live_total_frames(a, audio), audio.sample_rate());
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

double playhead_pixel_x(const AppState& a, const GuiAudio& audio,
                        int64_t vp_start, double spp) {
    (void)audio;
    if (spp <= 0.0) return -1.0;
    return static_cast<double>(a.playhead_cursor_sample - vp_start) / spp;
}

double playhead_pixel_x(const AppState& a, const GuiAudio& audio) {
    return playhead_pixel_x(a, audio, a.viewport_start_sample,
                            current_samples_per_pixel(a, audio));
}

double scanner_pixel_x(const AppState& a, const GuiAudio& audio,
                       int64_t vp_start, double spp) {
    (void)audio;
    if (spp <= 0.0) return -1.0;
    // Drive the scanner's pixel from the CONTINUOUS predictor position
    // (playhead_scanner_precise) rather than the quantized integer sample, so a
    // per-frame viewport rescale during a strip-drag zoom slides the scanner
    // smoothly instead of jittering on integer-frame steps. The line still
    // paints as a hard 1px column at the rounded pixel; only its basis is
    // continuous. The 2-arg overload below delegates here.
    return (a.playhead_scanner_precise - static_cast<double>(vp_start)) / spp;
}

double scanner_pixel_x(const AppState& a, const GuiAudio& audio) {
    return scanner_pixel_x(a, audio, a.viewport_start_sample,
                           current_samples_per_pixel(a, audio));
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

    AppState     app;
    GuiAudio     audio;
    GuiPlayback  playback;
    GuiPlatform  gui;
    WaveformCache wf_cache;
    // Trim boundary stems live on their own surface, rebuilt synchronously from
    // on_tick (the marker stem is the selected-marker live overlay
    // paint_selected_stem — singleton-gated, blue, armed by hover/drag/nudge —
    // not cached). Constructed alongside wf_cache so they share the same lifetime;
    // passed by reference into GuiPaintHandler and (for the destroy_surface hook)
    // GuiFileLoader.
    StemCache     stem_cache;
    // Top-strip flag rects live on their own surface, rebuilt
    // synchronously from on_tick alongside the stem cache. Same lifetime
    // shape, same passed-by-reference plumbing.
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
    GuiPlaybackLifecycle playback_lifecycle(app, audio, gui, playback, viewport);
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
    GuiPaintHandler paint_handler(app, audio, playback, wf_cache, stem_cache,
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
    GuiSettingsEditor settings_editor(app, audio, viewport, active_views, undo,
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

    // Viewport worker kick: any viewport mutation (pan/zoom/center/follow)
    // requests the new waveform immediately rather than waiting for the next
    // tick. maybe_enqueue_waveform_render is main-thread-safe and idempotent
    // against the on_tick backstop, so the earlier trigger only shortens
    // input-to-render latency. See Viewport::kick_waveform_render.
    viewport.request_waveform_render_ =
        [&]() { paint_handler.maybe_enqueue_waveform_render(); };

    // Pure-pan fast-path: scroll_viewport drives this instead of the full
    // worker kick. Shifts the live plate by the pixel delta and renders only
    // the newly exposed edge strip inline, so fast touchpad scroll stays
    // continuous. Falls back to the worker for any non-pan case. See
    // Viewport::kick_waveform_pan and GuiPaintHandler::pan_waveform_incremental.
    viewport.request_waveform_pan_ =
        [&](int64_t new_vp, bool sync) {
            paint_handler.pan_waveform_incremental(new_vp, sync);
        };

    // One-shot discrete jumps route here instead of the async worker: render
    // the plate synchronously and publish the displayed fingerprint now so the
    // overlays and waveform land in the same frame. See
    // Viewport::kick_waveform_sync and
    // GuiPaintHandler::force_synchronous_waveform_rebuild.
    viewport.request_waveform_sync_ =
        [&]() { paint_handler.force_synchronous_waveform_rebuild(); };

    // Pointer capture: the input handler's begin/end hooks drive the platform's
    // cursor lock (pointer-constraints + relative-pointer). Shared by three
    // waveform/strip gestures — the zoom-strip drag, the ctrl-exact waveform
    // strip drag, and the alt-exact waveform pan — for infinite pan/zoom travel.
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
        if (app.playhead_scanner_restore_pending) {
            app.playhead_scanner_endpoint_painted = true;
        }
    });

    gui.set_on_resize([&](int w, int h) {
        // A compositor configure can change zoom, samples-per-pixel, and the
        // viewport beneath an in-flight positional drag whose grab anchor is a
        // frame coordinate computed from the old geometry, so the next motion
        // event would derive its delta across two different coordinate systems
        // and commit a spurious jump. Cancel any in-flight pointer drag before
        // on_resize applies, so the resize always lands on a gesture-free
        // state: cancel_active_drags is a no-op when no drag is active, so the
        // plain no-gesture resize path is unaffected. A live editor text-
        // selection drag is finalized up front by cancel_active_drags (collapsed
        // to a caret, selection-only, nothing to revert).
        input_handler.cancel_active_drags();
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
        // work dialog when dirty, same as Ctrl+Q. Cancel any in-flight
        // pointer drag before the prompt goes up, matching the Ctrl+Q
        // hatch: while the prompt is up the pointer handlers swallow motion
        // and release, so a drag left alive would commit on the next motion
        // if the user dismisses the prompt. cancel_active_drags is a no-op
        // when no drag is active, so the clean and non-drag close paths are
        // unaffected. A live editor text-selection drag is finalized up front
        // by cancel_active_drags (collapsed to a caret, selection-only, nothing
        // to revert), so there is no motion-free interval where it would swallow
        // keys until a later pointer motion noticed the lost button.
        input_handler.cancel_active_drags();
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

    // The shift-range anchor lives only across a continuous physical shift
    // hold; the platform's shift falling edge (incl. keyboard leave and
    // capability loss) is its release owner and dissolves it here.
    gui.set_shift_released_hook([&] { app.shift_range_anchor = -1; });

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
                // Mirror kick_waveform_sync's repair (codex P2 fix): a total
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

        // Stem-cache dirty-detect. Runs AFTER the waveform's
        // dirty-detect on purpose — both layers key their displayed-
        // viewport inputs off wf_cache.fp_*, so on a viewport-change
        // tick the waveform enqueues and the stems hold the OLD
        // viewport; on a post-swap tick (eventfd handler runs before
        // on_tick) wf_cache.fp_* already carries the new viewport, so
        // stems snap together with the just-blitted waveform.
        paint_handler.maybe_rebuild_stem_cache();
        // Flag-rect cache dirty-detect. Same ordering rule as
        // the stem cache — keyed off wf_cache.fp_* so flags, stems, and
        // waveform all snap together at the worker's completion swap.
        paint_handler.maybe_rebuild_flag_cache();

        // Nudge stem-pin reap (architect 2026-07-23). The blue nudge pin dies by
        // TWO causes (see AppState::stem_pin_*): (a) command adjacency lost — any
        // other command bumped command_seq — or (b) the burst window lapsed. Paint
        // stops drawing the pin the instant EITHER holds, but nothing guarantees a
        // waveform repaint: a command that bumps command_seq may damage only
        // another strip (bare `o` invalidates just the bottom strip), and an idle
        // pin whose window lapsed gets no command at all. So this reaper is the
        // one-shot cleanup for BOTH: when the pin is set and either cause holds,
        // damage the waveform so the blue line disappears without user input and
        // reset stem_pin_marker so it fires ONCE. Full-area damage (not a column):
        // the stem painted on the DISPLAYED basis (wf_cache.fp_*), but a column
        // recomputed on the LIVE basis (app.viewport_start_sample + current spp)
        // can miss it inside a resize/async publish window, ghosting the old
        // column. The reap is rare (once per burst end), so one full repaint is
        // negligible and harmless when the killing command already damaged the
        // waveform (a redundant damage-union).
        if (app.stem_pin_marker >= 0 &&
            (app.command_seq != app.stem_pin_command_seq ||
             monotonic_ms() - app.stem_pin_ms >
                 static_cast<int64_t>(kGestureCoalesceMs))) {
            viewport.invalidate_waveform_area();
            app.stem_pin_marker = -1;
        }

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
            // that stutters playhead motion at high zoom. The timestamp area is
            // invalidated only by the pre-paint hook (when the
            // predictor advances past app.playhead_scanner_sample),
            // never by the tick — the tick fires ~2x per frame, so
            // duplicating the timestamp rect here is wasted on_redraw
            // work.
            const double px = scanner_pixel_x(app, audio);
            invalidate_playhead_columns(px, px);
            return;
        }

        if (app.playhead_scanner_restore_pending) {
            if (!app.playhead_scanner_endpoint_painted) {
                // Self-arm: the endpoint paint has not been acknowledged yet.
                // Re-schedule the hold's damage (scanner column, timestamp,
                // top strip) so a paint is guaranteed to run on_redraw, which
                // sets endpoint_painted; the following tick then restores. The
                // timer tick is free-running but this branch otherwise schedules
                // nothing, so without re-arming, any future path that dropped
                // the hold's damage or reset endpoint_painted mid-handshake would
                // wedge the scanner on the endpoint permanently. Same damage set
                // as hold_natural_end_scanner; the top-strip rect is always
                // onscreen, so a paint is always produced.
                const double px = scanner_pixel_x(app, audio);
                invalidate_playhead_columns(px, px);
                invalidate_timestamp_area();
                const GuiRect ts = top_strip_area(app);
                gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
                return;
            }
            playback_lifecycle.restore_playhead_to_lsp();
            if (app.follow_mode && !app.follow_overridden_for_session)
                follow_scroll_if_needed();
            return;
        }

        // Playing was true last tick, now false — natural end. Hold the
        // scanner on the exclusive end bound for one paint, then deactivate
        // it on the following tick (no snap-back; once inactive its value
        // fields are stale by contract). In target view the end
        // bound is the bound target buffer's exclusive domain end — playback's
        // domain offset travels with the bind, so domain_end() is exactly the
        // full-target-frame coordinate the session played to.
        if (app.playhead_scanner_active) {
            const int64_t endpoint =
                (app.active_audio_view == 'T' &&
                 app.target_buffer_frames > 0)
                    ? playback.domain_end()
                    : viewport.trim_end_sample();
            playback_lifecycle.hold_natural_end_scanner(endpoint);
            if (app.follow_mode && !app.follow_overridden_for_session)
                follow_scroll_if_needed();
        }
    });

    gui.set_on_pre_paint([&]() {
        if (app.loading || audio.total_frames() <= 0) return;
        if (!playback.is_playing()) return;

        // Loop wrap: a looping audition (trim set, launch-captured in the
        // shared launch body launch_playback_from — Space toggle and scrub
        // launch alike) wrapped its read position back to the window
        // begin. That is a backward cursor jump the free-running predictor
        // cannot see (it clamps its prediction to end_sample, so cursor() holds
        // at the window end and never reveals the wrap), so resync the
        // predictor to the wrapped audio cursor here — the ruled resync event
        // for the wrap (playback.h head comment). Detected via the audio
        // thread's monotonic wrap counter rather than a cursor snapshot for
        // exactly that reason. The normal scanner advance below then reads the
        // post-resync cursor() (now at the window begin) and invalidates the
        // old->new column span for the backward jump. Looping no longer needs
        // follow mode: the resync and invalidation run regardless, and the
        // advance's follow_scroll_if_needed() simply no-ops when follow is off
        // (the scanner may wrap to an offscreen column — normal follow-off
        // playback, nothing else assumes follow-on for the loop).
        const uint64_t wrap_seq = playback.loop_wrap_seq();
        if (wrap_seq != app.playback_loop_wrap_seen) {
            app.playback_loop_wrap_seen = wrap_seq;
            playback.resync_predictor();
        }

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

        // One-shot read of the viewport-mutation stash. When set, it
        // holds the scanner's last painted pixel-x under the OLD
        // viewport; the recomputed scanner_pixel_x against the new
        // viewport would point at a column the scanner was never
        // painted at, leaving a ghost. old_px reads the still-current
        // playhead_scanner_precise (the last painted continuous position).
        double old_px;
        if (app.playhead_scanner_old_px_stash >= 0.0) {
            old_px = app.playhead_scanner_old_px_stash;
            app.playhead_scanner_old_px_stash = -1.0;
        } else {
            old_px = scanner_pixel_x(app, audio);
        }
        // Advance both fields: the integer sample (domain / change-detection)
        // and the continuous position the scanner pixel is drawn from. new_px
        // and the invalidated span are therefore computed from the continuous
        // pixel, matching what paint draws.
        app.playhead_scanner_sample = cur;
        app.playhead_scanner_precise = playback.cursor_precise();
        const double new_px  = scanner_pixel_x(app, audio);

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
