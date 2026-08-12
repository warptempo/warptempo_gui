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
#include "history_commit_worker.h"
#include "history_prefetch.h"
#include "audio.h"
#include "waveform_worker.h"
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
#include "text_editor.h"
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
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace {

// redesign_font_size_px() (render.h) and bottom_row_pad_x() (paint_handler.h)
// live where paint_handler.cpp can reach them; the constants
// below are paint-handler-independent and stay file-local.

// The strip/lane geometry is a fixed-pixel per-strip lane stack derived from
// the nine authored gui_scale row heights (menu_row_h_px() ... the row-5 trio,
// plus bottom_row_h_px() and transport_row_h_px()), kRowGapPx and
// kFlagBottomLiftPx (see the geometry helpers below); nothing is
// window-proportional, and since row 7 nothing is font-proportional either.

// The pointer grab tolerances live beside the surfaces they belong to —
// kMarkerStemGrabPx in app_state.h (reached by the hit_test_* free functions
// and the GuiInputHandler mouse handler), kTrimEndcapGrabPx in render.h — so
// nothing of that family is file-local here.

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
// DialogTrigger, PromptState, ViewState,
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
// flexes between them. The TOP strip is SEVEN lanes (from the window edge
// inward): MENU ROW (its own authored menu_row_h_px(), row 1 of the kdenlive
// redesign), TOOLBAR ROW (its own authored toolbar_row_h_px(), row 2 of the
// redesign), TAB ROW (its own authored tab_row_h_px(), row 3), ICON ROW (its
// own authored icon_row_h_px(), row 4), then row 5's three — the TRIM lane
// (trim_lane_h_px(), the bar and its endcaps), the RULER lane
// (ruler_lane_h_px(), timestamps + tick tops + the zoom strip's drag band) and
// the MARKER lane (marker_lane_h_px(), the flags, their stems and the PLAYHEAD
// HEAD on the lane's bottom rows — the head moved down out of the ruler at the
// row-5 live test, though the ruler painter still draws it, needing the tick
// columns), whose bottom edge is the
// waveform top. ALL SEVEN ride the gui_scale axis: row 5 retired the last
// font-scaled lanes in this strip. The BOTTOM strip is TWO lanes since row 8
// (2026-08-11): the TRANSPORT ROW (transport_row_h_px(), the eight-button
// bottom toolbar, flush under the waveform) over the STATUS row — which was
// the strip's ONE lane from row 7 (2026-08-01), when the status row and the
// editor/modal row COLLAPSED INTO ONE LINE, bottom_row_h_px() tall. Both ride
// the gui_scale axis like every other redesigned row, so no lane anywhere is
// font-scaled any more.
// The lanes pack tight — the inter-lane gaps kRowGapPx and the
// outer/waveform-side gaps
// kFlagBottomLiftPx are all 0 — and the derivation below keeps them explicit so
// the stack reappears correct if either constant is ever un-zeroed. That
// property has NO EXCEPTION LIST among the consumers: every lane-anchored pixel
// reaches its band through a lane rect computed here. The
// render.cpp sites that stack marker geometry take their LANE RECT as a
// parameter, resolved by their callers from top_marker_row_area — the same rect
// the empty-lane press gate tests — so painted flags and flag hit rects move
// with the lane by construction and cannot drift from each other.
//
// THE FLAG/TRIANGLE SEAM INVARIANT IS RETIRED (row 5, 2026-08-01). It read:
// those two lanes were CONTIGUOUS BY INVARIANT because a marker flag was ONE
// FUSED GLYPH — rectangle plus tip-down triangle under a single continuous
// outline — spanning both, so a gap there would have cut one asset through its
// middle. The fused glyph is gone: a marker is now a single text-on-flag BOX
// inside ONE lane, and the playhead's triangle became the aliased head on the
// MARKER lane's bottom rows. No seam is exempt any more — every seam (menu|toolbar, toolbar|tab,
// tab|icon, icon|trim, trim|ruler, ruler|marker, and both outer
// kFlagBottomLiftPx gaps) is honored structurally by the loop below and by every
// consumer, with no asset spanning any of them. ONE shared
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
// Per-lane pixel heights, indexed from each strip's window edge inward. EVERY
// LANE IN BOTH STRIPS is now an authored crop-measured constant on the GUI-SCALE
// axis (menu_row_h_px(), toolbar_row_h_px(), tab_row_h_px(), icon_row_h_px(),
// row 5's trio, and — since row 7 — bottom_row_h_px()); no lane sizes from a
// font metric any more. The two-axes ruling and what became of the font axis are
// at those accessors' declarations in render.h.
//
// The toolbar, tab and icon lanes INCLUDE their 1px border-bottom, and the
// bottom lane BOTH its borders (the CSS box model: the architect's stated
// content height excludes its borders, and the lane owns every pixel it paints).
constexpr int kTopLaneCount    = 7;
constexpr int kBottomLaneCount = 2;
int top_lane_height(int lane) {
    switch (lane) {
        case 0: return menu_row_h_px();          // menu row (proportional text)
        case 1: return toolbar_row_h_px();       // toolbar row (+ border-bottom)
        case 2: return tab_row_h_px();           // tab row (+ border-bottom)
        case 3: return icon_row_h_px();          // icon row (+ border-bottom)
        // ROW 5 REPLACED THE FOUR LEGACY LANES WITH THREE (2026-08-01). The old
        // trim-chip / marker-text / flag / triangle stack is gone: the chips
        // became the trim BAR, the marker-text lane died with its occlusion
        // resolver, and the flag+triangle pair became ONE marker lane carrying
        // kdenlive's text-on-flag boxes. All three size on the gui_scale axis
        // from their own crop-measured constants, like lanes 0-3 — so the LAST
        // font-scaled lane in the top strip went with them.
        case 4: return trim_lane_h_px();         // trim bar + endcaps
        case 5: return ruler_lane_h_px();        // timestamps / ticks / zoom strip
        // Flags, stems and the playhead head; bottom edge = waveform top.
        case 6: return marker_lane_h_px();
        default: return 0;
    }
}
// The bottom strip's TWO lanes, indexed from the window edge inward like the
// top strip's: lane 0 is the STATUS line (ROW 9 in the architect's numbering
// since the transport row became row 8; it landed as row 7 — its 1px top
// border and its
// 1px bottom border on the window's last row), lane 1 is the TRANSPORT ROW
// (row 8, 2026-08-11 — the eight-button bottom toolbar and its 1px border-top
// on the waveform side). The bottom strip was ONE lane from the 2026-08-01
// collapse until row 8 opened the touch arc.
int bottom_lane_height(int lane) {
    switch (lane) {
        case 0: return bottom_row_h_px();       // status line (+ both borders)
        case 1: return transport_row_h_px();    // transport row (+ border-top)
        default: return 0;
    }
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
    // DEFENSIVE NON-NEGATIVE FLOOR on the height, and it is a SILENT-WRONG guard
    // in the ruled sense: no stderr, no refusal, no clamp of anybody's settings.
    //
    // THE LANE STACK IS SCHEMA-LEGAL PAST THE WINDOW: gui_scale at its 200
    // ceiling doubles all nine lanes (roughly 250 px of top strip plus 160 of
    // bottom), which the supported 1080-tall window still holds — but the guard
    // does not rest on that arithmetic, because the ceiling is a vocabulary the
    // architect moves and the lane set is one the redesign keeps adding to. If
    // the sum ever exceeds the window this subtraction goes NEGATIVE on it.
    // A negative-height rect is a silent-wrong input to every consumer
    // that takes a width/height pair, which is exactly the class this project
    // keeps a guard for; the absurd-but-legal combination is allowed to look
    // broken (strips overlapping out the bottom of the window) but is not
    // allowed to compute nonsense.
    //
    // ZERO, not a positive minimum: zero is the honest answer ("no room left"),
    // and every consumer already handles it — the painters gate on `w <= 0 ||
    // h <= 0`, the plate render and the flag/waveform caches skip an empty area,
    // playhead_invalidate_rect yields an empty rect (whose tick fallback damages
    // the bottom strip instead), and cairo treats an empty rectangle as a no-op.
    // A positive floor would instead invent a strip of waveform that has nowhere
    // to live. THE VOCABULARY QUESTION WAS ANSWERED SEPARATELY — gui_scale's
    // ceiling came down to 200 (architect 2026-07-31), and font_size, the other
    // half of the cross-product this guard was written against, left the schema
    // entirely in row 7 — and the guard STAYS regardless: it costs one compare
    // and it is the class of fault (silent-wrong geometry) the project keeps
    // guards for.
    const int h_avail = h - top_h - bot_h;
    return GuiRect{0, top_h, effective_w, h_avail < 0 ? 0 : h_avail};
}

// ONE shared layout contract for every strip lane — the single geometry owner.
// A lane is a pure index from its strip's window edge (0 = the edge-most lane):
// the outer gap kFlagBottomLiftPx sits between the window edge and lane 0, and
// each successive lane is one prior-lane height + one inter-lane gap kRowGapPx
// further inward. The top strip counts downward from y=0; the bottom strip
// mirrors it about the window midline (`h - inset - lane_h`).
//
// Paint/hit agreement invariant: the TRIM BAR is TOP lane 4 (the ruler is lane
// 5 and the marker lane lane 6), and hit_test_trim_endcap / the pair-drag y-gate
// read top_trim_row_area(app) — the exact band render_trim_flags paints the
// lane ground, the window's bar and its two endcaps in, because the PAINTER is
// handed that band as a parameter (GuiPaintHandler::paint_trim passes
// top_trim_row_area(app) as render_trim_flags' `trim_bar`) instead of
// re-deriving a lane y from the row heights above it. Both sides therefore
// reach the band through this one helper, so paint and hit cannot drift when a
// lane above the trim bar changes height, is removed, or gains a gap.
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
// Lane 0 is the MENU row (the kdenlive menu bar at the window edge: a flat
// ground carrying the left float's Quit/Settings and the right float's view
// bar, plus its own 1px margin-bottom); lane 1 is the TOOLBAR row (the flat
// ground carrying the Save / Undo / Redo / Render buttons, its separators and its
// border-bottom); lane 2 is the TAB row (the "A" / "B" Breeze tabs and
// its border-bottom); lane 3 is the ICON row (the seventeen view/mode/action
// buttons and its border-bottom); lane 4 is the TRIM lane (the bar, its
// endcaps, every trim gesture the b/e chips used to carry, and the span-framing
// double-click); lane 5 is the RULER lane (the timestamp ladder, the reborn
// zoom strip); lane 6 is the MARKER lane (the flags, their stems, and the
// playhead's aliased head on the lane's bottom rows), whose bottom edge is
// flush with the waveform area top. (The trim and ruler lanes were ONE merged
// input band — top_trim_surface_area — for the trim surface arc's one day,
// 2026-08-11..12; the revert re-split them and deleted the accessor.)
GuiRect top_menu_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 0);
}

GuiRect top_toolbar_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 1);
}

GuiRect top_tab_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 2);
}

GuiRect top_icon_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 3);
}

GuiRect top_trim_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 4);
}

GuiRect top_ruler_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 5);
}

GuiRect top_marker_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 6);
}

// THE BOTTOM STRIP IS TWO LANES since row 8 (2026-08-11). Lane 0 — the window
// edge — is the STATUS row (ROW 9 in the architect's numbering since row 8
// landed; it landed as row 7, architect 2026-08-01): the status row and
// the modal/editor row collapsed into a single line carrying the
// active modal / editor / prompt / status text when one applies, plus the
// critical chip ahead of it (the dirty flag moved off the
// row entirely — it is the window title's dot now, and the TIMESTAMP left for
// row 8's centre on 2026-08-11). Lane 1, inward of it and
// flush under the waveform, is the TRANSPORT ROW (row 8, the touch arc's first
// surface): the eight-button bottom toolbar and the clock between its two
// groups, whose accessor is below.
// bottom_row_area is that lane INCLUDING both 1px borders,
// as the strip stack allocates it; bottom_row_content_area is the ground between
// them, the band every painter and baseline works in.
//
// (The former pan-strip row retired earlier — pan lives on the Alt+drag waveform
// grab and the ctrl+waveform strip drag's horizontal axis.)
GuiRect bottom_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/false, 0);
}

GuiRect bottom_row_content_area(const AppState& a) {
    const GuiRect lane = bottom_row_area(a);
    const int b = bottom_row_border_h_px();
    return GuiRect{lane.x, lane.y + b, lane.w, lane.h - 2 * b};
}

// THE TRANSPORT ROW (row 8, 2026-08-11): bottom lane 1, directly under the
// waveform area — the lane INCLUDING its 1px border-top, as the strip stack
// allocates it. Paint and hit agree through this one accessor exactly as the
// top rows' do through theirs.
GuiRect bottom_transport_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/false, 1);
}

// Resolve the SOURCE-view trim NAVIGATION range from AppState's trim
// pair — one of the TWO range owners (Viewport::trim_range is the target-view
// half), and the reason the full window behaves exactly as the old unset state
// did everywhere downstream. PLAYBACK IS NO LONGER A CONSUMER (2026-08-05): the
// source-view audition plays to the SONG's end and the target-view one to the
// bound preview buffer's, both decided at playback_lifecycle.cpp, so what this
// pair still bounds is Home/End, the load-time playhead and the span framing.
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
// per frame, so consumers (Home/End via trim_range, the load-time playhead, the
// span framing) still degrade to a no-op or a per-side position and must not
// assume begin <= end.
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

// The narrow playhead-damage rect. TWO CONSUMERS, and the rect is REAL DAMAGE
// at the first of them, not a predicate: Viewport::invalidate_playhead_columns
// builds one per column (old and new), unions them when they are close and
// invalidates what comes out; the tick's offscreen fallback in this file reads
// only the WIDTH, as an emptiness test. Both are the narrow-on-plate shape,
// reserved for the two per-frame scanner sites (the rule and the per-site table
// are at playhead_pixel_x, app_state.h). The half-width is playhead_half_px()'s
// to own — render.h states its authored value, its provenance, and the recorded
// mismatch against the wider marker-lane head.
GuiRect playhead_invalidate_rect(const GuiRect& area, double px_x) {
    const int col = static_cast<int>(std::nearbyint(px_x));
    const int x0 = std::max(area.x, col - playhead_half_px());
    const int x1 = std::min(area.x + area.w, col + playhead_half_px() + 1);
    if (x1 <= x0) return GuiRect{area.x, 0, 0, 0};
    // Envelope extends up from the top of the window to the bottom of the
    // waveform area so it covers the playhead's stem inside the waveform AND
    // its top-strip half above — the aliased head, which since row 5 stands in
    // the MARKER lane's bottom rows with its tip on the waveform boundary (it
    // moved out of the ruler lane at the 2026-08-01 live test; the occlusion
    // rationale is at the paint site, paint_handler.cpp), plus the marker-lane
    // stem segment that shares the head's block and is zero-height while the
    // tip sits on that boundary. That top-strip half is where the cursor's
    // non-waveform pixels live, and the envelope covers the whole lane band
    // above the waveform rather than tracking the head's own rows.
    const int y0 = 0;
    const int y1 = area.y + area.h;
    return GuiRect{x0, y0, x1 - x0, y1 - y0};
}

// THE STATUS LANE'S RECT — bottom lane 0 (row 9), borders included, since the
// lane owns them. What lives on it is section C's precedence chain and the
// critical chip; the TIMESTAMP left it for row 8 on 2026-08-11, so a clock
// advance no longer touches this lane at all (the two lanes' owners are
// contrasted at the declaration, app_state.h). The transport row above damages
// itself through its own face writers and the clock cell below.
// (The dirty flag is not a sharer either: it lives in the window title, which
// the compositor repaints, so a dirty transition damages nothing of ours.)
GuiRect status_row_invalidate_rect(const AppState& a) {
    return bottom_row_area(a);
}

// THE CLOCK'S RECT — row 8's reserved centre cell as the painter last drew it
// (AppState::clock_cell_rect, whose stash contract is at the field). Narrow by
// construction: on_redraw clips to the damage region, so paint_transport_row
// runs but its eight buttons fall outside the clip and cost nothing, which is
// what makes this affordable at the pre-paint hook's per-frame cadence.
//
// BEFORE THE ROW'S FIRST PAINT the stash is zero and the answer is the WHOLE
// transport lane — the honest widening, and unreachable in practice: the first
// frame damages the window entire.
GuiRect clock_invalidate_rect(const AppState& a) {
    const GuiRect cell = a.clock_cell_rect;
    if (cell.w <= 0 || cell.h <= 0) return bottom_transport_row_area(a);
    return cell;
}


int main(int argc, char** argv) {
    if (!verify_c_numeric_locale("warptempo_gui")) return 1;

    // Auto-reap the fire-and-forget external audio players the `l`
    // ("Listen to renders") command spawns. Ignoring SIGCHLD makes the kernel
    // discard child exit status so the detached players never linger as zombies;
    // set once here, never per-press.
    //
    // TWO SUBSYSTEMS FORK NOW, and both are written to this disposition rather
    // than against it. The players are the fire-and-forget half. THE HISTORY MODE
    // (src/gui/history_diff.cpp) is the other: it runs git synchronously through
    // two fenced entry points — reads for the diff and the walk, and the commit
    // act's `add`/`commit`/`push` — and because this disposition makes waitpid
    // return ECHILD, it decides nothing from child status. A read reports whether
    // it RAN (an exec self-pipe) and what it said; the commit act's verdicts are
    // observations of the repository afterwards. Both helpers' comments own the
    // reasoning; what matters here is that neither depends on the default
    // disposition being restored.
    std::signal(SIGCHLD, SIG_IGN);

    // Ignore SIGPIPE so a broken pipe is an EPIPE return rather than a process
    // kill — what GTK and Qt do for the same reason. THE LIVE PRODUCER is the
    // clipboard data-source `send` callback (platform_wayland.cpp): it writes
    // the payload into a pipe fd the CONSUMER owns, and a consumer that closes
    // its read end before or during the transfer would otherwise terminate the
    // GUI outright, the write loop's EPIPE arm never reached. With the signal
    // ignored that loop sees the short/failed write it is already written for
    // (abandon the transfer, close the fd, no state to unwind). libjack's
    // server socket is the same shape and inherits the same protection — it has
    // no SIGPIPE-dependent behaviour of its own. The spawned external player
    // does NOT inherit this: the `l` launch resets SIGPIPE to default alongside
    // SIGCHLD (spawn_audio_player, input_key_dispatch.cpp), since an ignored
    // disposition survives exec and would change the child's own semantics.
    std::signal(SIGPIPE, SIG_IGN);

    // A source is loaded only from the command line: the GUI has no
    // in-session file open or drag-and-drop (open the next source by
    // relaunching). The audio path is therefore mandatory — there is no
    // blank-window state to load into.
    if (argc != 2) {
        std::fprintf(stderr, "Usage: warptempo_gui <audio_file>\n");
        return 1;
    }
    const char* cli_path = argv[1];

    // (NO PALETTE LOAD HERE ANY MORE. The colors were 23 mutable globals filled
    // from ~/.config/warptempo_gui/colors.conf by load_color_config() at exactly
    // this point — before the first paint and before anything could derive a
    // value from them. The whole system retired 2026-08-02: the palette is
    // constexpr, so there is nothing to initialize and every reader, the
    // waveform worker thread included, sees compile-time constants. The record
    // is at the palette block, render.h.)

    AppState     app;
    GuiAudio     audio;
    GuiPlayback  playback;
    GuiPlatform  gui;
    WaveformCache wf_cache;
    // Top-strip flag rects live on their own surface, rebuilt
    // synchronously from on_tick. Constructed alongside wf_cache so they share
    // the same lifetime; passed by reference into GuiPaintHandler. (Trim has no
    // cache — every trim pixel paints live per frame in
    // GuiPaintHandler::paint_trim; marker stems are the
    // live overlay paint_marker_stems, off this cache's own published stash.)
    FlagCache     flag_cache;
    if (!gui.init(app.width, app.height, "warptempo_gui")) {
        return 1;
    }

    // -- Viewport + invalidation helpers ------------------------------------
    //
    // The viewport-mutation and invalidation helpers are methods on the
    // Viewport struct (viewport.{cpp,h}), including the bottom strip's two
    // lane owners, invalidate_status_row_area and invalidate_clock_area. Every other
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
            "warptempo_gui: Failed to start async renderer; exiting\n");
        return 1;
    }
    // Waveform-cache rebuild runs on this dedicated worker; the
    // paint thread becomes blit-only. Must be constructed before
    // GuiPaintHandler (which takes it as a reference).
    GuiWaveformWorker waveform_worker;
    if (!waveform_worker.init()) {
        std::fprintf(stderr,
            "warptempo_gui: Failed to start waveform worker; exiting\n");
        return 1;
    }
    // THE CHECKPOINT WORKER (2026-08-07): the `h` history view's Save-and-Commit
    // act runs its git steps here instead of on the GUI thread, which used to
    // freeze the window for as long as the remote took. Single job in flight,
    // completion delivered through its own eventfd below, and shutdown() JOINS
    // an act in progress at quit (the state is saved before the act is
    // dispatched at all, so the wait loses nothing).
    GuiHistoryCommitWorker history_commit_worker;
    if (!history_commit_worker.init()) {
        std::fprintf(stderr,
            "warptempo_gui: Failed to start the checkpoint worker; exiting\n");
        return 1;
    }
    // THE HISTORY WALK'S PREFETCH WORKER (2026-08-07): the `h` view's commit
    // walk is built here, at startup and off the GUI thread, instead of at every
    // `h`. It streams its members back through its own ready fd (wired below,
    // after the input handler exists — the drain has a reaction as well as a
    // store to fill). Fatal on a failed init like its three siblings: with no
    // worker there is no walk, and the view would open empty forever.
    GuiHistoryPrefetch history_prefetch;
    if (!history_prefetch.init()) {
        std::fprintf(stderr,
            "warptempo_gui: Failed to start the history prefetch worker; "
            "exiting\n");
        return 1;
    }
    // Shared process-local render cache for target-view reuse, archival
    // reuse/publish rungs, and the loaded-in-place render's survival after
    // the renders folder is wiped. init() creates the per-process cache
    // directory under
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
    // Paint handler constructed before file_loader, which applies gui_scale
    // changes through its on_resize (the shared geometry-and-cache rebuild
    // path). The settings-editor gui_scale commit uses the input handler's own
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
    GuiSaveOps save_ops(app, undo, active_views);
    GuiPrompt prompt(app, gui, viewport,
                     phase_reset_propagate, save_ops, playback_lifecycle);
    GuiSettingsEditor settings_editor(app, audio, viewport, selection,
                                      active_views, undo,
                                      target_render, playback_lifecycle);
    gui.set_worker_completion_fd(async_renderer.completion_fd(),
        [&async_renderer]() { async_renderer.on_completion_event(); });
    gui.set_waveform_worker_completion_fd(waveform_worker.completion_fd(),
        [&waveform_worker]() { waveform_worker.on_completion_event(); });
    gui.set_history_worker_completion_fd(history_commit_worker.completion_fd(),
        [&history_commit_worker]() {
            history_commit_worker.on_completion_event();
        });
    GuiInputHandler input_handler(app, audio, gui, playback,
                                  viewport, selection, undo,
                                  warpops, phase_resets, marker_drag,
                                  flag_editor,
                                  renders_dir, active_views,
                                  phase_reset_propagate,
                                  async_renderer,
                                  history_commit_worker,
                                  history_prefetch,
                                  playback_lifecycle, save_ops, prompt,
                                  settings_editor, target_render,
                                  paint_handler);
    // Back-wire the settings editor to the input handler (constructed after the
    // editor, which the input handler holds by reference — the cycle is
    // resolved with a pointer set here). The editor reaches
    // handle_active_audio_view_toggle / apply_gui_scale / commit_trim_mutation
    // through it, so a `:`-typed GUI key funnels into the same gesture code.
    settings_editor.input = &input_handler;
    // Same back-wire for the phase-reset propagate: its paste tail lands in
    // target view through handle_active_audio_view_toggle, the chokepoint that
    // lives on the input handler (constructed after the propagate, which the
    // input handler holds by reference — the cycle is resolved with this
    // pointer set).
    phase_reset_propagate.input = &input_handler;
    // The prefetch's ready fd is wired HERE rather than beside the other three,
    // because its callback needs the input handler: a drain fills the store AND,
    // while the view stands, measures the head delta and damages the window for
    // a walk that just grew.
    gui.set_history_prefetch_completion_fd(history_prefetch.completion_fd(),
        [&input_handler]() { input_handler.on_history_prefetch_ready(); });
    // (NO BACK-WIRE FOR THE PROMPT. It had one while the history mode's commit
    // confirmation lived there — its `y` reached the act through the pointer —
    // and both went with the prompt on 2026-08-07, the act now being run by the
    // commit-title editor's Enter inside the input handler itself.)

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
    // stays agnostic to whether capture is available. The begin hook forwards the
    // gesture's own cursor kind, which is what the release restores (contract at
    // GuiPlatform::begin_pointer_capture).
    input_handler.begin_strip_pointer_capture = [&](GuiCursorKind restore_kind) {
        gui.begin_pointer_capture(restore_kind);
    };
    input_handler.end_strip_pointer_capture   = [&]() { gui.end_pointer_capture(); };
    input_handler.set_strip_capture_restore_x = [&](double sx) { gui.set_capture_restore_x(sx); };

    // The touch navigation (touch phase 1, 2026-08-11; THREE hooks since the
    // windowed model's return, the sixth glass ruling 2026-08-12 — the
    // trim-move members of 2026-08-11 stay dead): the platform's nav frames —
    // two-finger centroid-pan + pinch-zoom, and the phone model's
    // single-finger pan frames born of a drag starting on the pan surface —
    // drive the input handler's ONE touch-nav body, which runs the
    // strip-drag family's own viewport chokepoint — the
    // set_keyboard_intent_cancel_hook wiring precedent, one narrow
    // platform-to-GUI hook set. The PAN-ZONE QUERY is the third hook: the
    // platform asks it once at each first finger's down, and the GUI answers
    // the waveform area, geometry only (refusals stay per-frame in the
    // update body). A one-finger gesture ANYWHERE ELSE needs no wiring: it
    // is translated into the ordinary pointer deliveries above, and nothing
    // on this side can tell which device produced them. Contracts at
    // GuiPlatform::set_touch_nav_hooks (the platform half) and at
    // apply_touch_nav_update's declaration (the GUI half, including why the
    // nav gesture stops short of the strip drag's pointer-press arm).
    gui.set_touch_nav_hooks(
        [&](int x, int y, double dx, double dist_ratio) {
            input_handler.apply_touch_nav_update(x, y, dx, dist_ratio);
        },
        [&]() { input_handler.end_touch_nav(); },
        [&](int x, int y) {
            return input_handler.touch_point_in_pan_zone(x, y);
        });

    auto invalidate_status_row_area  = [&]() { viewport.invalidate_status_row_area(); };
    auto invalidate_clock_area       = [&]() { viewport.invalidate_clock_area(); };
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
        // ANY FULL RELAYOUT CLOSES THE OPEN DROPDOWN, and a resize is the
        // one relayout edge that can reach it: it moves the menu row and every
        // published rect with it, so a popup left open would hang from a button
        // no longer under it. The other relayout-class events cannot happen
        // while it is open — a file load is startup-only, and a tab switch or a
        // gui_scale commit both need the keyboard, which the popup gate
        // swallows. Ordered with finalize_active_drags above and the layout
        // below, so the resize still lands on a gesture-free, popup-free state.
        input_handler.close_dropdown();
        paint_handler.on_resize(w, h);
        // (THE CURSOR RE-RESOLVE THAT STOOD HERE IS GONE, 2026-08-03. This
        // callback force-ends the gestures and rebuilds the layout — both facts
        // the cursor's zone map reads — and it used to owe a refresh at its
        // tail, ordered after BOTH writes above. It owes nothing now: a configure
        // is dispatched inside a run-loop iteration whose tail re-derives the
        // cursor from the settled state, so the ordering rule this comment used
        // to spell is a property of the loop rather than of this placement.)
    });

    auto invalidate_top_strip     = [&]() { viewport.invalidate_top_strip(); };

    // popup_eligible_marker is a free function in app_state.{h,cpp}. Its two
    // remaining callers reach it directly with the (app, idx) signature — the
    // bottom strip's resolved readout (paint_handler.cpp) and the Ctrl+C copy
    // (input_handler.cpp), the two surfaces that took the SELECTION translation
    // when the hover machinery was deleted. The iteration-mode part of its gate
    // is documented above its declaration in app_state.h.

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
        // THE HINT GOES DOWN WITH IT, and this is the SAME RULE AS THE KEY-PRESS
        // HIDE rather than a new one: no floating hint stands over a modal. Every
        // KEYBOARD opener implements it at the top of on_key; this compositor
        // close is THE ONE modal opener that arrives asynchronously — it carries
        // no key and no pointer event to hide with — so the rule needs its call
        // here or the hint stands over the prompt until the tick's dwell refusal
        // catches it a frame later. (The checkpoint worker's failure report was a
        // second such opener from 2026-08-07 until 2026-08-09, when it became the
        // bottom row's paint-only critical slot and stopped raising anything.)
        // Ordered ABOVE request_close so the box's published rect is
        // damaged before the prompt's own repaint, and beside the popup close for
        // the reason below — the two floating surfaces go down together.
        input_handler.hide_shift_tooltip();
        // THE POPUP GOES DOWN BEFORE THE PROMPT GOES UP — including its armed
        // item and the menu row's mode, all three being the one close owner's
        // job. Without this the two would stand together and ownership would
        // SPLIT: the prompt takes keys and presses (its gates are tested first),
        // but motion reaches the DROPDOWN branch, which sits above the prompt's,
        // and a left RELEASE reaches finish_dropdown_release, which sits above
        // the prompt gate in on_button_release — so an item pressed and still
        // HELD when the compositor close arrived would fire on release and raise
        // the settings editor UNDERNEATH the prompt. Closing here makes "the
        // prompt outranks the dropdown" structural in all four input channels
        // instead of an ordering accident in two of them.
        // THE RESIZE PATH DOES THE EQUIVALENT for the same class of reason (a
        // popup that cannot stay coherent through what follows), and Ctrl+Q needs
        // no line of its own: it reaches the popup's own keyboard gate first,
        // which closes the menu and only then lets the close route run. (The
        // checkpoint notice's opener owed this same pair from 2026-08-08 until
        // 2026-08-09; it raises no modal now, so these two routes are again the
        // whole list.)
        input_handler.close_dropdown();
        prompt.request_close();
        // (The cursor re-resolve this callback used to end with is gone for the
        // same reason the resize path's is — see there. The prompt this may have
        // just raised is one of the zone map's own refusals, and the map is asked
        // again at this iteration's tail, past everything above.)
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

    // Pointer-leave / capability-loss drop. THIS BODY IS THE AUTHORITATIVE
    // EFFECT LIST for the hook — tooltip hide, roster click face, the popup's two
    // item faces plus its press claim, and the PAIR that a leave through row 1
    // skips, the roster hover clear and the menu-row disarm (which is itself
    // gated a second time, on no menu being open). The platform-side sites name
    // their OWN concern and point
    // here rather than each keeping a list that can drift (the setter contract
    // and the member comment in platform_wayland.h, and the capability-loss fire
    // site in platform_wayland.cpp).
    // THE TWO EDGES ARE NOT THE SAME EDGE (codex 2026-08-03) — and SINCE
    // 2026-08-08 THE BODY IS TOLD WHICH ONE IT IS, the platform handing in a
    // GuiPointerLeaveReason, because one effect below now differs between them.
    // (SINCE TOUCH PHASE 1, 2026-08-11, a touch pointer translation's end
    // fires this hook too — as OrdinaryLeave, and ONLY on its no-focus arm
    // since codex round 3: with the physical pointer focused, the platform
    // delivers a restore MOTION at the mouse's own position instead and this
    // body never runs — the ordinary motion path re-derives hover and the
    // settled cursor from truth (the fork's one statement is at
    // deliver_touch_translation_end, platform_wayland.cpp). On the arm that
    // DOES fire, the body needed no change:
    // clearing hover faces where a finger last was is precisely what the
    // no-hover-under-touch consequence asks for, and the row-1 keep below
    // reads the remembered position exactly as it does for a mouse. The fire
    // sites are the touch edge inventory's, platform_wayland.h.)
    // The difference itself is the reason none of the clears may lean on "no
    // later event". Only
    // CAPABILITY LOSS ends that pointer stream outright — no motion and no
    // release will ever arrive on the object again. AN ORDINARY LEAVE has no
    // position event only WHILE the pointer stays outside: the platform PRESERVES
    // `pointer_left_held_`, a re-entry synthesizes a motion, and a still-held
    // button releases normally afterward. WHAT MAKES EVERY CLEAR HERE SAFE is
    // that each drops a VISUAL FACE or a press CLAIM, so a later motion or
    // release lands unowned or as a harmless no-op — never an inability of those
    // events to arrive. WHAT MAKES THE ONE KEPT FACE SAFE is the other half of
    // the same sentence, and it holds on the soft edge ONLY: the return motion
    // that re-derives it exists there and nowhere else.
    // The hover-driven faces must therefore be cleared here — EXCEPT on the one
    // leave named below, which keeps its button lit on purpose — or a pointer
    // that slides out of the window over a button leaves its pill / outline lit
    // for as long as it stays outside. THE MARKER
    // HOVER USED TO RIDE THIS EDGE TOO and no longer exists (row 5) — the
    // redesigned rows' button hover is the only ROSTER hover left (an open
    // dropdown's item hover is the other pointer-position-dependent surface
    // this edge drops, below), and it is separate state with its own clear.
    // Row 2's CLICK face joins it: this is the BUTTON-LOST edge as far as the
    // FACE is concerned — the hold stops being the pointer's to show — and a
    // stranded pressed interior would outlive it. A release that does arrive
    // later (the ordinary-leave case) finds the face already cleared and clears
    // nothing, on_button_release's own top call being transition-gated too:
    // the harmless no-op named above. Both are transition-gated.
    // AN OPEN POPUP'S TWO FACES GO WITH THEM, and only they: the item under
    // the pointer is lit by `hovered_item` with no in-window term in the
    // painter, and the armed item by `pressed_item`, so both would outlive the
    // pointer that named them — a flick out of the
    // window whose last on-surface motion was still over an item leaves it lit
    // until a RETURN motion recomputes. One call clears both faces AND the press
    // claim (clear_dropdown_pointer_state), and dropping the claim is what
    // leaves a re-entry's motion and any later release owning nothing; the MENU
    // ITSELF STAYS UP, because leaving the window is not a dismissal.
    // THE MENU ROW'S MODE ENDS HERE TOO, BUT NOT WHEN THE POINTER LEAVES THROUGH
    // ROW 1 (the armed bit, AppState::Dropdown::menu_row_armed; architect
    // 2026-08-08). Once a menu has been opened from row 1 the anchors open on
    // hover alone, and a pointer that has left the window has left the visit, so
    // coming back must take a click again — the same rule the band-exit disarm
    // states, at the coarser edge. It is a no-op while a menu is OPEN (the gate is
    // inside disarm_menu_row): leaving the window is not a dismissal, and the
    // popup that stays up stays the mode.
    // THE EXCEPTION IS THAT SAME SENTENCE APPLIED TO THE CLOSED-AND-ARMED STATE.
    // Row 1 ABUTS THE TITLEBAR, so the commonest way to leave the window from the
    // row is to slide one pixel UP off it — and the mode's own band question,
    // asked of the remembered position (point_in_menu_row_band, the exact
    // predicate the motion exit uses), answers "still on the row". Leaving the
    // window that way is no more a dismissal than leaving it with a menu standing
    // is: that case already keeps the popup up, keeps the mode, and behaves
    // stickily on return, and the two must not disagree over which pixel the
    // pointer crossed. So on that leave the mode SURVIVES and the hovered row-1
    // button KEEPS ITS FACE — Quit stays lit under a pointer resting on the
    // titlebar, which is the visible half of the rule — and the first motion back
    // in re-derives hover normally and, over an anchor, opens its menu (the armed
    // hover open at on_motion's tail).
    // THE FACE IS KEPT WHOLESALE, not per button, because with the pointer inside
    // row 1's band no OTHER roster button can be hovered: hover is resolved from
    // that one position against disjoint rects, so "the faces standing at the
    // leave" is exactly "the row-1 button under the pointer, if any".
    // NOTHING ELSE MAY CLEAR IT WHILE THE POINTER IS OUT, and one refusal covers
    // that: recompute_redesign_button_hover — the tick's per-frame repair and the
    // only writer of these bits that runs without a pointer event — returns early
    // while app.pointer_in_window is false (stated there). A RESIZE still ends the
    // mode, deliberately: it runs close_dropdown, which clears the armed bit above
    // its own early return, and a relayout is a real dismissal. Any OTHER leave —
    // below the row, or with the mode not armed — behaves exactly as it always
    // did.
    // THE EXCEPTION IS SCOPED TO THE ORDINARY LEAVE, and that is a correctness
    // term rather than tidiness (codex 2026-08-08): this body is shared with
    // POINTER-CAPABILITY LOSS, the hard end of the stream, where no leave, no
    // motion and no release will ever arrive again. Keeping anything there would
    // strand it — a lit row-1 button with no event left to unlight it (the
    // in-window refusal above, the very thing that protects the kept face, would
    // then also be what prevents its repair), an armed mode with no pointer, and
    // a later capability RETURN whose first motion over an anchor would spring a
    // menu open with no click ever given. So the reason the platform hands in
    // (GuiPointerLeaveReason) is the first term of the test: the ruled titlebar
    // trip keeps its face and its mode, the hard end of the stream keeps nothing
    // and runs the unconditional clear and disarm.
    // THE TOOLTIP GOES DOWN ON THIS EDGE TOO, and it must go down HERE rather
    // than be left to the tick's hover recompute: the hint hangs BELOW the top
    // strip, and hide_shift_tooltip is the only route that damages the box's own
    // published rect as well as the strip. The hover clear below queues STRIP
    // damage alone, so a repaint running between this event and the next tick
    // would find no hovered owner, publish a zero rect and return — leaving the
    // part of the box below the strip in the buffer with no rect left to erase
    // it with. Hiding in the same event that takes the pointer away makes the
    // erase and the unhover one edge — and the tick is not a fallback for it in
    // any case: that recompute refuses outright while the pointer is outside, so
    // this is the only hide the edge gets.
    gui.set_pointer_left_hook([&](GuiPointerLeaveReason reason) {
        // Read the band BEFORE the in-window flag goes false: the answer is about
        // the remembered position, which this hook does not touch, but the two
        // reads belong together and the order says which leave this is. The
        // REASON is the first term: only the ordinary leave has the return motion
        // the exception is built on.
        const bool through_menu_row =
            reason == GuiPointerLeaveReason::OrdinaryLeave &&
            app.dropdown.menu_row_armed &&
            point_in_menu_row_band(app, app.last_mouse_x, app.last_mouse_y);
        app.pointer_in_window = false;
        input_handler.hide_shift_tooltip();
        if (!through_menu_row) {
            input_handler.clear_redesign_button_hover();
            input_handler.disarm_menu_row();
        }
        input_handler.clear_redesign_button_press();
        input_handler.clear_dropdown_pointer_state();
    });

    // WINDOW-ACTIVATION EDGE -> the redesigned header's ground swap. The hook
    // fires only when the xdg_toplevel state actually flips (the platform owns
    // the edge test), so this is a mirror-and-damage pair with no comparison of
    // its own. It takes the pointer-leave hook's shape for the pointer-leave
    // hook's reason: a protocol edge that changes what should be on screen and
    // carries no other event to repaint it.
    // TOP-STRIP damage is the exact rect — rows 1 and 2 are the only surfaces
    // that read the flag, and both live there.
    gui.set_activation_changed_hook([&] {
        app.window_activated = gui.window_activated();
        viewport.invalidate_top_strip();
    });

    // THE PLATFORM'S CONSUMED KEYBOARD EDGES end the transport arrows'
    // hold-repeat (codex round 4, 2026-08-11): keyboard leave / keyboard-
    // capability loss and every Super-swallowed press are key events the GUI's
    // three chokepoint disarms can never see — the platform consumes them
    // without calling on_key — so the platform reports them through this one
    // hook instead of the application growing a second, partial list. The fire
    // classes and the per-swallowed-delivery decision are at the setter's
    // contract (platform_wayland.h); the consumer's authoritative edge
    // inventory is at AppState::transport_repeat. The body is the disarm
    // itself: no damage (the hold has no face of its own — the click face is
    // cleared by its own edges) and no other state.
    gui.set_keyboard_intent_cancel_hook([&] {
        app.transport_repeat.owner = -1;
    });

    // THE SETTLED BOUNDARY AND ITS TWO CONSUMERS (architect 2026-08-03,
    // replacing the per-site model). The run loop fires this at the TAIL of every
    // iteration it is not leaving, so whatever is derived here is derived once per
    // poll wakeup from a state that has fully settled — after the display's
    // events, the tick and both worker completions.
    //
    // WHAT BELONGS HERE IS ONE CLASS: a POINTER-DERIVED FACE whose INPUTS can
    // move with no pointer event under them. Such a face cannot be maintained by
    // pushes at the sites that move its inputs — the set of those sites is not
    // enumerable and stays wrong — and the loop boundary answers all of them at
    // once. This body IS the authoritative list of what rides the boundary; the
    // platform-side contracts name only their own concern and point here.
    //
    // THE POINTER CURSOR WAS THE FIRST CONSUMER and is still the one this
    // boundary was built for.
    // WHAT IT REPLACED, and why the replacement is structural rather than one
    // more site: the kind used to be PUSHED from twenty-three places — the top of
    // on_motion, eight release arms, ten button-lost and refused-begin arms in
    // on_motion, the two force-end callbacks above, a modifier-edge hook in the
    // platform, and a scope guard in on_key — each owing the rule "run after the
    // state you read has settled". The zone map reads about ten independent fact
    // families, so a refresh was owed by
    // every writer of any of them: a set nobody could enumerate and keep
    // enumerated, and two review rounds each found a class the previous
    // derivation had missed while whole classes (a wheel zoom moving the trim
    // endcaps under a resting pointer, the zoom and navigation keys, `x`, an
    // undo restoring trim, `o`, a gui_scale relayout, every keyboard editor open
    // and close, a dropdown item click) had no site to hang a call on at all.
    // A LOOP BOUNDARY IS AFTER EVERY SETTLE BY DEFINITION, so all of them are
    // answered by this one call and the ordering rule itself is gone.
    //
    // THE COST IS ONE MAP EVALUATION PER WAKEUP and no protocol traffic while
    // the answer holds: GuiPlatform::set_cursor_kind applies only on a change.
    // refresh_pointer_cursor is a no-op while the pointer is outside the window,
    // and it does nothing but re-derive and apply — no hover recompute, no
    // damage, no gesture logic. `mods` is the platform's live modifier truth,
    // handed over rather than fetched.
    //
    // THE OPEN DROPDOWN'S ITEM FACES ARE THE SECOND CONSUMER (2026-08-03, from
    // codex), and they are the same disease at a different surface: the popup's
    // item rects are PAINTER-PUBLISHED, zero from the open until paint_dropdown
    // publishes them, so a motion delivered before that paint resolves NO item —
    // and if the pointer then RESTS, no further motion exists to correct it.
    // Press Settings, hold, slide down onto an item's position inside the frame
    // the menu came up in, stop moving, release after it paints: nothing was ever
    // armed, nothing was lit, and the release ran nothing — permanently, not for
    // one frame. Rects moving under a stationary pointer is exactly what this
    // boundary answers, so the iteration that PAINTS them ends by resolving hover
    // and arm against them, under the live button state the hook already carries.
    // recompute_dropdown_hover returns immediately with no menu open and damages
    // only on a change, so the standing cost is a compare per wakeup.
    // IT DOES NOT REPLACE on_motion's call: that one is per DELIVERED MOTION, and
    // a dispatch batch can carry a motion and then the PAINT that reads these
    // faces with no loop tail between them. What neither caller has to be is LAST
    // before a release: the release derives its item from its own coordinates
    // (finish_dropdown_release), precisely because this walk's inputs can move at
    // an in-batch paint. The two callers answer different questions; the reasoning
    // is at the definition.
    //
    // THE TWO ARE INDEPENDENT, so the order here is free: the zone map refuses
    // every cue while a popup is open and reads neither item face, and the
    // recompute writes nothing the map reads.
    // NEITHER RE-LIGHTS WHAT THE POINTER-LEAVE HOOK ABOVE DROPPED: each refuses on
    // app.pointer_in_window inside its own body, so a per-iteration call cannot
    // resurrect a face from coordinates the pointer has left behind.
    gui.set_loop_settled_hook([&](GuiInputState mods) {
        input_handler.refresh_pointer_cursor(mods);
        input_handler.recompute_dropdown_hover(mods);
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
            // THE PREFETCH'S STARTUP KICK (2026-08-07), here because this is
            // where the source settles: app.source_audio_path and
            // app.projects_repo are both final by now (the sidecars applied
            // above), and the scan needs exactly those two. It costs the GUI
            // thread one queue push — everything else happens on the worker
            // while the user is still looking at the first frame.
            input_handler.kick_history_prefetch();
            return;  // loaded state paints on the next tick
        }

        // End-of-run check for the "Updating..." label's hold: a stretch of
        // quiet with no output-affecting trigger in it means the user stopped,
        // so the run ends and the held label clears once the work is idle
        // (target_render.h's two run constants carry the rule).
        //
        // THE TICK, NOT THE SETTLED HOOK, and not any event site. It has to be a
        // clock somebody reads unbidden: a run ends by nothing happening, and
        // the last trigger of a run is indistinguishable from the middle of one
        // when it arrives, so no event exists to hang this on. The tick is the
        // right unbidden reader — the timerfd is a free-running ~125 Hz interval
        // (arm_playback_timer, half the refresh period), armed whether or not
        // anything is playing and independent of input and of frame callbacks,
        // so the quiet is noticed within a tick of the window expiring even on a
        // completely idle desktop. It is also where the product's other
        // millisecond display timeouts already live (the button hover dwell
        // below). The settled hook was considered and refused: its documented
        // class is POINTER-DERIVED FACES whose inputs move with no pointer event
        // under them, and this is neither pointer-derived nor a face.
        // Placement inside the tick is free — it reads only its own run state
        // and the status slot — but it sits above the paint invalidations so its
        // clear lands in the same frame they do.
        target_render.tick_updating_hold();

        // Its sibling for the ARCHIVAL status message: promote the message
        // parked at dispatch once the worker reports that synthesis actually
        // began, so a render served by one of do_render's reuse rungs shows
        // nothing (architect 2026-08-08). The tick for the same reason as the
        // hold, from the other direction: the hold watches for an event that
        // never comes (quiet), this one watches for an event on the WORKER
        // thread that wakes nothing on ours — the completion eventfd fires only
        // when the render is over. Both are polls of settled state, both cost a
        // compare, and keeping them adjacent keeps the two status owners' tick
        // work in one place.
        input_handler.tick_promote_render_status();
        input_handler.tick_render_cancel_face();

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
        // (load / load-in-place), so no NAMED asynchronous case remains —
        // preview
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
                    viewport.invalidate_clock_area();
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

        // THE REDESIGNED BUTTONS' STATE-VECTOR STALENESS COMPARATOR — the ONE
        // site that repairs a stale DISABLED or SELECTED face, and the reason no
        // route that pushes/pops history, toggles read-only, flips follow or
        // iteration mode, or finishes a load carries an invalidate of its own.
        // Those facts change with no top-strip damage whatsoever, so the strip
        // would keep showing yesterday's faces; here the LIVE bits are compared
        // against the ones the painter last painted
        // (RedesignButtonFace::enabled and ::selected) and any drift pays a
        // single invalidate_top_strip. The repaint rewrites the whole stash, so
        // this settles in one pass — including the cold case, where the roster's
        // bits start at their defaults and the first compare corrects them.
        // ONE MECHANISM, BOTH HALVES: the selected half joined with row 4 rather
        // than growing a second comparator, because it is the same problem — a
        // painted bit whose source mutates with no damage — with the same
        // answer. `f` and `i` are the pure cases (a bare flag flip and nothing
        // else); `t` and `p` happen to damage anyway and still go through here
        // rather than being trusted to.
        // Placed ABOVE the loading/blank return below on purpose: loading and
        // total<=0 are themselves inputs to the enabled predicate, so the
        // transition INTO and OUT OF a load is exactly a drift this must catch.
        // THE HOVER TOOLTIP'S DUE-CHECK — the whole timer, and it is two number
        // comparisons on a tick that already runs. The hover recompute stamped
        // hover_ms when a tooltip-bearing button was entered (and zeroed it on
        // every exit / press / wheel through hide_shift_tooltip), so all that is
        // left is: is a dwell running, has it come due, and is the tooltip not
        // already up. One invalidate on the show edge; the HIDE edge is owned by
        // the input side, which knows the box's painted rect. No timer object,
        // no callback, no per-frame damage.
        if (!app.redesign_tooltip.visible && app.redesign_tooltip.hover_ms != 0 &&
            monotonic_ms() - app.redesign_tooltip.hover_ms >= kTooltipDelayMs) {
            app.redesign_tooltip.visible = true;
            // The show edge cannot know the box's own rect yet (the paint that
            // publishes it is the frame this schedules), so it damages the
            // owner's strip plus the full-width band the box can hang into —
            // at most tooltip_damage_h_px() tall. The band's SIDE follows the
            // owner: a top-row tooltip hangs BELOW the top strip, a transport-
            // row one (row 8, 2026-08-11) hangs ABOVE its lane, the painter's
            // own flip. The HIDE edge has the published rect and damages
            // exactly that.
            if (app.redesign_tooltip.owner >= 0 &&
                redesign_button_in_transport_row(static_cast<RedesignButton>(
                    app.redesign_tooltip.owner))) {
                const GuiRect tr = bottom_transport_row_area(app);
                viewport.invalidate_rect(tr);
                viewport.invalidate_rect(GuiRect{
                    0, tr.y - tooltip_damage_h_px(), app.width,
                    tooltip_damage_h_px()});
            } else {
                invalidate_top_strip();
                const GuiRect ts = top_strip_area(app);
                viewport.invalidate_rect(
                    GuiRect{0, ts.y + ts.h, app.width, tooltip_damage_h_px()});
            }
        }

        {
            // Row 8's damage fork, here too (2026-08-11): a drifting transport
            // face damages its own bottom-strip lane, everything else the top
            // strip. Each strip pays only for its own drift — the walk keeps
            // going until both verdicts are known (or the roster ends). The
            // play/stop pair is this comparator's newest customer: a natural
            // end-of-song clears playhead_scanner_active with no damage of its
            // own, and this is what swaps the pair's faces back.
            bool drift_top       = false;
            bool drift_transport = false;
            for (int i = 0;
                 i < kRedesignButtonCount && !(drift_top && drift_transport);
                 ++i) {
                const RedesignButton id = static_cast<RedesignButton>(i);
                const AppState::RedesignButtonFace& f = app.redesign_buttons[i];
                const bool drifted =
                    f.enabled  != redesign_button_enabled(
                                      app, audio.total_frames(), id) ||
                    f.selected != redesign_button_selected(app, id);
                if (!drifted) continue;
                if (redesign_button_in_transport_row(id))
                    drift_transport = true;
                else
                    drift_top = true;
            }
            if (drift_top) invalidate_top_strip();
            if (drift_transport)
                viewport.invalidate_rect(bottom_transport_row_area(app));
        }

        // HOVER IS NO LONGER MOTION-ONLY. It is resolved from the pointer's last
        // position against the painter's rects and redesign_button_hoverable —
        // and BOTH of those move without any pointer event: a keyboard Ctrl+Tab
        // makes the tab under a stationary pointer hoverable (it was the
        // selected one, which does not hover), an enabled-state change makes a
        // greyed button hoverable, and a live gui_scale commit relays out every
        // rect under a pointer that never moved. Each of those left the face
        // wrong until the next motion.
        //
        // The fix is to run the ONE recompute here as well: it is transition-
        // gated internally and damages only on a real change, so a per-tick call
        // is a handful of rect compares and, on the frames that matter, exactly
        // the same single invalidate a motion would have paid. It runs AFTER the
        // comparator so it reads the freshest published stash.
        //
        // GATED ON "no pointer gesture", which preserves the standing rule that
        // an ACTIVE GESTURE FREEZES HOVER — the motion path enforces that by
        // returning before its tail, and an ungated tick would quietly undo it.
        // A pointer that has LEFT the window is handled inside the recompute
        // (it returns early on app.pointer_in_window), so the tick can neither
        // re-light what the leave hook cleared nor clear the row-1 face that hook
        // deliberately keeps when the pointer left through the menu row.
        if (!any_pointer_gesture_active(app))
            input_handler.recompute_redesign_button_hover();

        // ROW 8's ARROW HOLD-REPEAT (2026-08-11): while a transport-row arrow
        // button is physically held, this synthesizes its chord on the
        // keyboard repeat's labwc-matching cadence, stamped as a repeat so the
        // undo coalescing is the held key's own rule. One int compare when
        // idle; every firing condition (the hold, the pointer on the button,
        // the enabled bit, the schedule) lives in the body. Deliberately NOT
        // gated on any_pointer_gesture_active: the held button IS a live
        // pointer act, and the rows' presses arm no gesture that predicate
        // names.
        input_handler.tick_transport_arrow_repeat();

        // Stationary-cursor hover refresh (the BACKSTOP). A keyboard mutation
        // (tempo step, Ctrl+N, nudge) changes the hovered marker's fields/position
        // without a pointer-motion event, so nothing else re-reads the hover text.
        // THE HOVER REPAIR TICK IS GONE (row 5). A per-frame arm stood here
        // driving one recompute_hover_at_cursor whenever either marker store's
        // generation or the displayed-map generation moved past what the hover
        // cache had stamped — the backstop for a store mutation or a silent map
        // promotion under a stationary cursor. There is no hover cache to keep
        // honest any more: a marker's value is painted on its own flag, and the
        // readout and the copy both read the live store through the selection.

        // Blink the editor cursor independently of playback. Compare the
        // current visibility against the last painted state and invalidate
        // the top strip when it flips. Cheap: top_strip is small.
        if (text_editor::is_active(app.top_flag_editor)) {
            const bool now_visible =
                text_editor::cursor_visible_now(app.top_flag_editor);
            if (now_visible != app.top_flag_editor_blink_last) {
                app.top_flag_editor_blink_last = now_visible;
                if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket)
                    invalidate_status_row_area();
                else
                    invalidate_top_strip();
            }
        }
        // Same shape for the bottom-strip settings prompt; invalidate the
        // status lane on each visibility flip.
        if (text_editor::is_active(app.settings_editor)) {
            const bool now_visible =
                text_editor::cursor_visible_now(app.settings_editor);
            if (now_visible != app.settings_editor_blink_last) {
                app.settings_editor_blink_last = now_visible;
                invalidate_status_row_area();
            }
        }
        // Same shape for the bottom-strip load prompt.
        if (text_editor::is_active(app.load_editor)) {
            const bool now_visible =
                text_editor::cursor_visible_now(app.load_editor);
            if (now_visible != app.load_editor_blink_last) {
                app.load_editor_blink_last = now_visible;
                invalidate_status_row_area();
            }
        }
        // And for the history view's commit-title editor.
        if (text_editor::is_active(app.commit_title_editor)) {
            const bool now_visible =
                text_editor::cursor_visible_now(app.commit_title_editor);
            if (now_visible != app.commit_title_editor_blink_last) {
                app.commit_title_editor_blink_last = now_visible;
                invalidate_status_row_area();
            }
        }

        // (THE DEFERRED CHECKPOINT NOTICE'S POLL stood here from 2026-08-07 until
        // 2026-08-09, when the architect replaced the acknowledge modal with the
        // bottom row's PERMANENT CRITICAL SLOT. A paint-only cell needs no poll
        // and no free strip to wait for: the completion writes the string and
        // damages the row, and the next paint shows it — so the pump is gone with
        // the modal it pumped.)

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
            // scanner column is onscreen the CLOCK CELL is
            // invalidated only by the pre-paint hook (when the
            // predictor advances past app.playhead_scanner_sample),
            // never by the tick — the tick fires ~2x per frame, so
            // duplicating the clock rect there is wasted on_redraw
            // work.
            // PLATE basis, not live: the scanner's pixels are plate-registered
            // (paint_scanner), so its damage resolves there too — the rule and
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
            // nothing, damage the CLOCK CELL instead: row 8 is always
            // onscreen, so a paint is always produced, and it is the
            // honest rect — the clock tracks the SCANNER's time
            // while playing, so it is frozen alongside the line for
            // exactly the same stretch. Follow mode is untouched: no
            // viewport work is added here, and a chasing viewport keeps
            // the column onscreen so the fallback simply never fires.
            if (playhead_invalidate_rect(waveform_area(app), px).w <= 0)
                invalidate_clock_area();
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
        // plate-registered (paint_scanner), so its damage resolves there —
        // the rule and the per-site shape table live at playhead_pixel_x
        // (app_state.h). NARROW is mandatory here (this is the per-frame
        // scanner advance) and this lambda reaches paint_handler, so the site
        // takes the narrow-on-plate shape rather than widening.
        const GuiPaintHandler::PlateViewportBasis pb =
            paint_handler.plate_viewport_basis();
        const int64_t pb_vp = static_cast<int64_t>(pb.vp_start);
        // OLD COLUMN, DERIVED — never stashed (the viewport-mutation stash
        // playhead_scanner_old_px_stash was retired 2026-07-30; do not reintroduce
        // one). The plate basis IS the answer by construction: paint_scanner
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
        invalidate_clock_area();
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
    // Blocks on an in-flight checkpoint rather than abandoning a git child
    // mid-act; the piece is already saved (the act saves before it dispatches),
    // so the wait costs a moment and never any work.
    history_commit_worker.shutdown();
    // The prefetch abandons its scan at the next candidate boundary rather than
    // being waited out: it writes nothing anywhere.
    history_prefetch.shutdown();
    // Remove this process's render-cache directory.
    render_cache.shutdown();
    return 0;
}
