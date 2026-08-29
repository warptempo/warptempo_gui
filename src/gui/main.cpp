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
#include "external_sync.h"
#include "history_commit_worker.h"
#include "history_prefetch.h"
#include "audio.h"
#include "device_config.h"
#include "waveform_worker.h"
#include "file_loader.h"
#include "flag_editor.h"
#include "gui_display_context.h"
#include "gui_main.h"
#include "input_handler.h"
#include "onscreen_keyboard.h"
#include "paint_handler.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "render.h"
#include "render_pipeline.h"
#include "renders_dir.h"
#include "active_views.h"
#include "ab_audition.h"
#include "folder_overlay.h"
#include "render_player.h"
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
#include "platform.h"
#include "project_model.h"
#include "locale_check.h"

#include <cairo/cairo.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

namespace {

// redesign_font_size_px() (render.h) and icon_row_pad_x() (paint_handler.h)
// live where paint_handler.cpp can reach them; the constants
// below are paint-handler-independent and stay file-local.

// The strip/lane geometry is a fixed-pixel per-strip lane stack derived from
// the eight authored gui_scale row heights (menu_row_h_px() ... the row-5
// trio, plus bottom_row_h_px() — the unified bottom row), kRowGapPx and
// kFlagBottomLiftPx (see the geometry helpers below); nothing is
// window-proportional, and since row 7 nothing is font-proportional either.

// The one pointer grab tolerance lives beside the surface it belongs to —
// kTrimEndcapGrabPx in render.h (the marker stems' grab constant died with
// their pointer surface, 2026-08-12) — so nothing of that family is
// file-local here.

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

// The BPM-sweep math primitives (BaseTempoScale + compute_base_tempo_scale,
// and the cell rewrite bpm_cell_warp_markers) live in input_handler.h so
// input_handler.cpp and flag_editor.cpp can reach them.

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
// is superseded). Top and bottom strips DIFFER in height; the waveform flexes
// between them UP TO A CLAMP, and TWO FLEXIBLE GAPS of blank window ground
// center it (the vertical rule below). The TOP strip is SEVEN lanes since the
// relayout's commit B moved the OVERVIEW STRIP up into the centered block
// (2026-08-12; the 2026-08-12 roster commit had taken it to six by deleting
// the TOOLBAR ROW, row 2 of the redesign — its Save / Undo / Redo / Render are
// the icon row's first group now) — from the window edge inward: MENU ROW (its
// own authored menu_row_h_px(), row 1 of the kdenlive redesign, PINNED at the
// window top), then THE CENTERED BLOCK's six: TAB ROW (tab_row_h_px(), row 3 —
// 30 content inside a 1px border at EACH edge since 2026-08-13, the top line
// being what the row had always been missing; render.h's constant carries the
// crops and the withdrawn ground ruling that briefly stood in its place),
// ICON ROW (icon_row_h_px(), row 4), the OVERVIEW STRIP
// (overview_lane_h_px(), the whole-song lane — ONE fixed tiny height on every
// host now, 24 content + a 1px border at EACH edge since 2026-08-13; the
// render.h constant carries the ruling, the top border's arrival and the
// deleted min/max pair), then row 5's three: the TRIM lane
// (trim_lane_h_px(), the bar and its endcaps — the one lane that also rides
// kTrimBarScalePercent, resting at 100 since the seventh glass ruling), the
// RULER lane
// (ruler_lane_h_px(), timestamps + tick tops + the navigation surface's lane
// band — the pending click / grab-pan, the shift former and the ctrl zoom
// since the eighth glass ruling; its dedicated zoom entry and its one-day
// region former both died 2026-08-12) and
// the MARKER lane (marker_lane_h_px(), the flags, their stems and the PLAYHEAD
// HEAD on the lane's bottom rows — the head moved down out of the ruler at the
// row-5 live test, though the ruler painter still draws it, needing the tick
// columns), whose bottom edge is the
// waveform top. ALL SEVEN ride the gui_scale axis: row 5 retired the last
// font-scaled lanes in this strip. The BOTTOM strip is ONE lane again: THE
// UNIFIED BOTTOM ROW, bottom_row_h_px() tall (architect-ruled 2026-08-12, rows
// 8 and 9 merged; THE ICON ROW'S OWN HEIGHT AND PADS since 2026-08-14) — the
// transport three at the left
// pad and, flush right, the four single-marker verbs with the Edit flag
// button, the Marker Measure and Add to Selection behind them, the
// marker-walk three and the four
// cardinal arrows, divided by
// two of the ruled separators (the roster commit's
// rearrangement, re-weighted 2026-08-15 and again at the 2026-08-18 relayout,
// which deleted the mode SWAP that put the history companions in the arrows'
// slots inside the `h` view). All at the icon row's boxes, the clock in
// monospace beside the transport's own separator —
// sitting AT THE WINDOW'S FOOT with the flexible gap 2
// between it and the waveform. (The strip was one lane from row 7's collapse,
// 2026-08-01, two from row 8, 2026-08-11, one at the unification, two again
// when the overview strip landed below the row that afternoon, and one from
// commit B.) It rides the
// gui_scale axis like every other redesigned row, so no lane anywhere is
// font-scaled any more.
//
// THE VERTICAL RULE — THE WAVEFORM IS CENTERED IN THE WINDOW AND HAS A MAXIMUM
// HEIGHT (architect 2026-08-12: the seventh glass ruling gave the clamp,
// kWaveformMaxHeightPx at render.h carrying the value and its bracket; the
// relayout's COMMIT B, dictated at session close, gave the centering and took
// the clamp 550 -> 500). The window stacks, top to bottom:
//   THE MENU ROW, pinned at the window top;
//   GAP 1 — flexible blank window ground;
//   THE CENTERED BLOCK — tab row, icon row, OVERVIEW STRIP, trim bar, ruler,
//     marker lane, then THE WAVEFORM, whose own thick bottom border (render_-
//     canvas's, taken FROM the waveform area) is the block's bottom edge;
//   GAP 2 — flexible blank window ground;
//   THE UNIFIED BOTTOM ROW at the window's foot, its 1px border-top the thin
//     border facing the gap.
//
// THE POSITIONING RULE: the block sits so THE WAVEFORM'S VERTICAL MIDPOINT IS
// THE WINDOW'S VERTICAL MIDPOINT — centered within the APP SURFACE, with no
// titlebar arithmetic anywhere, because "the labwc titlebar above and the panel
// below offset each other" (the architect's own reasoning). The derivation, all
// of it in the four functions below:
//   leftover = win_h - (menu + block-above-the-waveform) - bottom row
//              = centered_leftover_h; the waveform's own borders are INSIDE its
//                area, so the block's thick bottom border is not a term here
//                (counting it would double it),
//   W        = min(waveform_max_h_px(), max(0, leftover))  = waveform_clamped_h,
//   gap 1    = max(0, win_h/2 - (menu + block above the waveform) - W/2)
//              = top_flex_gap,
//   gap 2    = max(0, leftover - W - gap 1)                = bottom_flex_gap.
// WHEN CENTERING IS INFEASIBLE the gaps floor at 0 and the WAVEFORM absorbs the
// shortfall — one clean formula, no second constant: on a short window the
// leftover is under the clamp, so W takes all of it and both gaps are 0 (the Pi
// exactly). A window tall enough for the clamp but too shallow to center it
// (the top block being taller than the bottom row) rests gap 1 at 0 and puts
// the remainder in gap 2, top-heavy and harmless.
//
// THE TWO STACKS AT 100% (recomputed here, the one record; top lanes 193 =
// menu 31 + tab 32 + icon 47 + overview 26 + trim 9 + ruler 28 + marker 20, of
// which 162 is the block above the waveform; the MENU LANE is 31 since
// 2026-08-21, when its content went back to kdenlive's own 30 and everything
// below row 1 rose 4px in the lane table; the BOTTOM ROW 47 since
// 2026-08-14, when it took the icon row's 46px content in place of its own 50
// — every number below is re-derived from that table rather than adjusted):
//   1920x1080: leftover 840 -> waveform CLAMPED at 500, gap 1 = 97, gap 2 = 243
//     — 31 menu / 97 blank / 162 block / 500 waveform / 243 blank / 47 row,
//     the waveform still spanning y 290..790 about the window's midline 540
//     (the clamp fixes its height and the midpoint rule its centre, so the
//     four pixels the menu row gave back go into gap 1 and the centered
//     block does not move).
//   1024x600 (the Pi): leftover 360 -> waveform UNCLAMPED at 360, both gaps 0
//     — 31 / 0 / 162 / 360 / 0 / 47. Centering is infeasible there (the
//     midpoint rule would want gap 1 = -73), so the waveform keeps everything
//     and takes the four pixels itself, which is the rule's own floor rather
//     than a special case.
//
// THE TWO BLANK BANDS ARE WINDOW GROUND AND HIT NOTHING: render_background's
// chrome erase paints both and no lane painter covers them; a press in either
// falls to a consumed nothing (gap 1 through the top strip's empty-spot return,
// gap 2 through the press path's tail), the cursor map answers Arrow over both,
// and the wheel is inert in both (gap 1 by its own band in wheel_context's
// inert list — it lies INSIDE top_strip_area, which is a pan surface; gap 2
// needs no band, lying below every area that probe tests). ONE OWNER: the two
// gaps enter the geometry at exactly three expressions in this file —
// strip_row_rect's inset for top lanes 1..6 (gap 1, which every block lane
// sits below), top_strip_h's total and bottom_strip_h's total (which is what
// makes waveform_area's h - top - bottom arithmetic yield W with no second
// expression of the rule) — so every consumer (hit tests, paint, damage, the
// wheel probe, the touch pan zone) inherits the shifted y's through the lane
// accessors with no second site.
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
// MARKER lane's bottom rows. No seam is exempt any more — every seam (menu|tab,
// which is GAP 1's band since commit B and was the toolbar lane's until the
// roster commit deleted it, tab|icon, icon|overview and overview|trim (the
// strip's new neighbours; the icon|trim seam it split was a flexible-gap band
// for the seventh ruling's first hours and tight from the row unification to
// commit B), trim|ruler, ruler|marker, and both outer
// kFlagBottomLiftPx gaps) is honored structurally by the loop below and by every
// consumer, with no asset spanning any of them. ONE shared
// helper —
// strip_row_rect — is the single geometry owner; every named accessor delegates
// to it, so a lane is a pure index from its strip's window edge, and a bottom
// lane's y is its inset flipped about the window midline (the bottom row rests
// ON the window's foot since commit B — gap 2 is above it, in bottom_strip_h's
// total rather than in the lane inset).

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
// axis (menu_row_h_px(), tab_row_h_px(), icon_row_h_px(),
// row 5's trio, and — since row 7 — bottom_row_h_px()); no lane sizes from a
// font metric any more. The two-axes ruling and what became of the font axis are
// at those accessors' declarations in render.h.
//
// The TAB and OVERVIEW lanes INCLUDE their TWO 1px borders (one per edge,
// both pairs since 2026-08-13), the ICON lane its one (bottom-side), and the
// UNIFIED BOTTOM ROW its 1px border-TOP — the waveform side,
// its only border (the CSS box model: the architect's stated content height
// excludes its borders, and the lane owns every pixel it paints). The overview
// lane was border-free under its old bottom-strip home, wearing the waveform's
// own 2px rows at BOTH ends instead; commit B reduced that chrome to a single
// bottom line, and a top line returned the next day at 1px (the
// derivation is at kOverviewHeightPx, render.h).
constexpr int kTopLaneCount    = 7;
constexpr int kBottomLaneCount = 1;
int top_lane_height(int lane) {
    switch (lane) {
        // (THE TOOLBAR ROW — top lane 1, the labeled Save/Undo/Redo/Render
        // lane — WAS DELETED 2026-08-12, the grand relayout's roster commit:
        // its four buttons are the icon row's first group now, so
        // kTopLaneCount fell 7 -> 6 and every lane below the menu row
        // renumbered -1. Commit B then took the count back to 7 by moving the
        // OVERVIEW STRIP up into the block as lane 3, renumbering row 5's trio
        // +1 again. The vertical arithmetic re-derives through this table with
        // no second site — the current stacks are at the vertical rule above.)
        case 0: return menu_row_h_px();          // menu row (proportional text)
        case 1: return tab_row_h_px();           // tab row (+ 2 borders)
        case 2: return icon_row_h_px();          // icon row (+ border-bottom)
        // THE OVERVIEW STRIP's home since commit B: the whole-song lane between
        // the icon row and the trim bar, ONE fixed tiny height on every host
        // (content + a 1px border at each edge; the ruling, the deleted min/max
        // clamp pair and the top border's arrival are at kOverviewHeightPx,
        // render.h). Its height no longer depends on the window, which is why
        // this table takes no win_h any more.
        case 3: return overview_lane_h_px();     // overview strip (+ 2 borders)
        // ROW 5 REPLACED THE FOUR LEGACY LANES WITH THREE (2026-08-01). The old
        // trim-chip / marker-text / flag / triangle stack is gone: the chips
        // became the trim BAR, the marker-text lane died with its occlusion
        // resolver, and the flag+triangle pair became ONE marker lane carrying
        // kdenlive's text-on-flag boxes. All three size on the gui_scale axis
        // from their own crop-measured constants, like lanes 0-2 — so the LAST
        // font-scaled lane in the top strip went with them.
        case 4: return trim_lane_h_px();         // trim bar + endcaps
        case 5: return ruler_lane_h_px();        // timestamps / ticks / nav band
        // Flags, stems and the playhead head; bottom edge = waveform top.
        case 6: return marker_lane_h_px();
        default: return 0;
    }
}
// The bottom strip's ONE lane: THE UNIFIED BOTTOM ROW (2026-08-12, rows 8 and
// 9 merged: buttons left, clock centered; the succession is at that row's
// geometry block, render.h), resting ON the window's foot since
// commit B — GAP 2 sits ABOVE it, in bottom_strip_h's total rather than in
// this lane's inset, so lane 0 is flush with the window edge like the top
// strip's own lane 0. (The OVERVIEW STRIP was bottom lane 0 for the afternoon
// of 2026-08-12 and is top lane 3 now.)
int bottom_lane_height(int lane) {
    switch (lane) {
        case 0: return bottom_row_h_px();       // unified row (+ border-top)
        default: return 0;
    }
}
// The UN-GAPPED lane sum — the strip's lanes and their (zero) authored gaps
// alone, with NO flexible gap in it. Each strip's public height adds its own
// flexible gap to this (top_strip_h / bottom_strip_h below), so this stays the
// gap derivation's own input and the two can never be circular.
int strip_total_h(bool top_strip) {
    int sum = 2 * static_cast<int>(kFlagBottomLiftPx);  // outer + waveform-side gaps
    const int lanes = top_strip ? kTopLaneCount : kBottomLaneCount;
    for (int i = 0; i < lanes; ++i)
        sum += top_strip ? top_lane_height(i) : bottom_lane_height(i);
    sum += (lanes - 1) * static_cast<int>(kRowGapPx);   // inter-lane gaps
    return sum;
}
// THE LEFTOVER the waveform and the two flexible gaps share (the vertical rule
// above): the window less the whole top lane stack and less the bottom row. May
// be NEGATIVE on an absurd window (a lane stack taller than the window itself —
// the silent-wrong guard at waveform_area owns that case). Takes the CLAMPED
// window height, exactly as every other geometry entry point does.
int centered_leftover_h(int win_h) {
    return win_h - strip_total_h(/*top_strip=*/true)
                 - strip_total_h(/*top_strip=*/false);
}
// THE WAVEFORM'S HEIGHT: the leftover, CLAMPED at the maximum (the seventh
// glass ruling's clamp, kWaveformMaxHeightPx at render.h). The floor at 0 is
// what keeps a degenerate window's negative leftover out of the gap arithmetic
// below; waveform_area's own guard answers the rect.
int waveform_clamped_h(int win_h) {
    const int leftover = centered_leftover_h(win_h);
    if (leftover <= 0) return 0;
    const int cap = waveform_max_h_px();
    return leftover < cap ? leftover : cap;
}
// GAP 1 — the flexible blank band between the MENU ROW and the centered block
// (commit B's centering rule, spelled at the vertical rule above): whatever it
// takes to put the waveform's midpoint on the window's, floored at 0 where that
// is infeasible. strip_total_h(top) is exactly the rule's "menu + the block
// above the waveform", the two being the whole top lane stack.
int top_flex_gap(int win_h) {
    const int gap = win_h / 2 - strip_total_h(/*top_strip=*/true)
                              - waveform_clamped_h(win_h) / 2;
    return gap > 0 ? gap : 0;
}
// GAP 2 — the flexible blank band between the waveform's bottom border and the
// UNIFIED BOTTOM ROW at the window's foot: the REMAINDER of the leftover, which
// is what makes the stack add up to the window exactly. Zero whenever the
// waveform took the whole leftover (every window short of the clamp — the Pi).
// The floor is defensive only: gap 1 can never exceed the remainder, since that
// would need the bottom row to be taller than the menu row plus the whole block
// above the waveform.
// (bottom_strip_flex_gap's successor, and top_strip_flex_gap's before it: the
// seventh ruling opened ONE flexible gap between the icon row and the trim
// lane, the row unification moved it whole to the window's foot below the
// bottom row, and commit B split it in two around the block.)
int bottom_flex_gap(int win_h) {
    const int gap = centered_leftover_h(win_h) - waveform_clamped_h(win_h)
                                               - top_flex_gap(win_h);
    return gap > 0 ? gap : 0;
}
} // namespace

// The TOP strip's public height INCLUDES GAP 1 (commit B): it is the distance
// from the window top to the WAVEFORM top, which is what every consumer asks of
// it — top_strip_area then spans the blank band between the menu row and the
// block (its damage covering it is correct: the band is repainted window
// ground), and waveform_area's y is this sum with no second expression.
int top_strip_h(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    return strip_total_h(/*top_strip=*/true) + top_flex_gap(h);
}
// The BOTTOM strip's public height INCLUDES GAP 2 (2026-08-12: the row
// unification made this the blank foot below the row, commit B moved the blank
// above it): it is the distance from the waveform bottom to the window bottom,
// which is what every consumer actually asks of it —
// bottom_strip_area then spans the blank band (its damage covering it is
// correct: the band is repainted window ground), and waveform_area's
// h - top - bottom arithmetic yields the CLAMPED, CENTERED waveform height with
// no second expression.
int bottom_strip_h(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    return strip_total_h(/*top_strip=*/false) + bottom_flex_gap(h);
}

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
    // BOTH strip heights INCLUDE their flexible gap (commit B's two-gap
    // centering), so the h - top - bot arithmetic below yields the CLAMPED
    // waveform height — min(leftover, kWaveformMaxHeightPx-scaled) wherever the
    // leftover is non-negative — and the y lands the waveform flush under the
    // marker lane with no second expression of the vertical rule here.
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
    // THE LANE STACK IS SCHEMA-LEGAL PAST THE WINDOW: gui_scale at its 400
    // ceiling (2026-08-26) quadruples all eight lanes — the top strip's 193
    // authored px become 772 and the bottom row's 47 become 188 — which the
    // supported 1080-tall window still holds, but by 120 px rather than by
    // room to spare. The guard
    // does not rest on that arithmetic, because the ceiling is a vocabulary the
    // architect moves — it has now moved twice — and the lane set is one the
    // redesign keeps adding to. If
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
    // the clock cell instead), and cairo treats an empty rectangle as a no-op.
    // A positive floor would instead invent a strip of waveform that has nowhere
    // to live. THE VOCABULARY QUESTION WAS ANSWERED SEPARATELY — gui_scale's
    // ceiling came down to 200 (architect 2026-07-31) and went back up to 400
    // (architect approval 2026-08-26, for a 280 dpi tablet panel), and
    // font_size, the other
    // half of the cross-product this guard was written against, left the schema
    // entirely in row 7 — and the guard STAYS regardless, the ceiling's return
    // being exactly why: it costs one compare
    // and it is the class of fault (silent-wrong geometry) the project keeps
    // guards for. (NEITHER FLEXIBLE GAP CAN DEEPEN AN OVERFLOW: both floor at
    // 0 and both derive from the same leftover this subtraction reads, which is
    // negative exactly when the lane stack overruns the window — so a window
    // too short for its lanes computes a ZERO waveform here between two
    // zero-height gaps, rather than a negative one. The overview lane's
    // one-day reserve-ahead rule, the one term that could deepen an overflow,
    // died with commit B's fixed lane height.)
    const int h_avail = h - top_h - bot_h;
    return GuiRect{0, top_h, effective_w, h_avail < 0 ? 0 : h_avail};
}

// ONE shared layout contract for every strip lane — the single geometry owner.
// A lane is a pure index from its strip's window edge (0 = the edge-most lane):
// the outer gap kFlagBottomLiftPx sits between the window edge and lane 0, and
// each successive lane is one prior-lane height + one inter-lane gap kRowGapPx
// further inward — PLUS, for TOP lanes 1 and beyond, GAP 1 (the vertical rule at
// the head of this block: the menu row is pinned at the window top and the
// centered block hangs below the flexible blank band, so every lane from the tab
// row down carries it). The bottom strip's own gap 2 is NOT an inset here: its
// one lane rests on the window's foot, and the gap sits above it, inside
// bottom_strip_h. The top strip counts downward from y=0; the bottom strip
// mirrors it about the window midline (`h - inset - lane_h`).
//
// Paint/hit agreement invariant: the TRIM BAR is TOP lane 4 (the ruler is lane
// 5 and the marker lane lane 6 — the numbering the overview strip's arrival in
// the block restored), and hit_test_trim_endcap / the pair-drag y-gate
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
    // GAP 1 opens between the menu row and the centered block (the vertical
    // rule): every top lane from 1 down sits below it, and the bottom strip's
    // arithmetic never sees it. (The seventh ruling's own gap sat after the ICON
    // row for hours; the row unification moved it to the window's foot as the
    // bottom lanes' inset, and commit B split it into this one and gap 2.)
    if (top_strip && lane_from_window_edge >= 1)
        inset += top_flex_gap(h);
    const int lane_h = top_strip ? top_lane_height(lane_from_window_edge)
                                 : bottom_lane_height(lane_from_window_edge);
    const int y = top_strip ? inset : (h - inset - lane_h);
    return GuiRect{0, y, w, lane_h};
}

// THE TWO BLANK BANDS AS RECTS. Gap 1 alone has a consumer: wheel_context's
// wheel-inert band list, because the band lies INSIDE top_strip_area, which is
// one of that probe's pan surfaces — everything else about both bands is a
// fall-through (a press claims nothing, the cursor map answers Arrow,
// render_background paints them). GAP 2 DELIBERATELY HAS NO ACCESSOR: it lies
// below every area the wheel probe tests, so the no-context 0 already answers
// it, and a rect with no reader would be dead code.
// Derived from the two lane rects it lies between rather than from the gap
// function, so the band covers the (zero) inter-lane gaps around it too — a
// seam is non-lane ground exactly as the flexible band is, and the answer stays
// correct if kRowGapPx is ever un-zeroed.
GuiRect top_flex_gap_area(const AppState& a) {
    const GuiRect menu = strip_row_rect(a, /*top_strip=*/true, 0);
    const GuiRect tab  = strip_row_rect(a, /*top_strip=*/true, 1);
    const int y0 = menu.y + menu.h;
    const int gap_h = tab.y - y0;
    return GuiRect{0, y0, menu.w, gap_h > 0 ? gap_h : 0};
}

// Top strip lanes, counted down from the window top (index 0 = the window edge).
// Lane 0 is the MENU row (the kdenlive menu bar at the window edge: a flat
// ground carrying the left float's four menu buttons and
// the right float's view
// bar, plus its own 1px margin-bottom), and GAP 1 opens under it — every lane
// below is a member of THE CENTERED BLOCK. Lane 1 is the TAB row (the "A" / "B"
// Breeze tabs and
// its border-bottom); lane 2 is the ICON row (the twenty-six view/mode/action
// buttons — the deleted toolbar row's four lead them since the 2026-08-12
// relayout, whose roster commit removed that lane and renumbered these, and
// the history group's seven close them since 2026-08-18 —
// and its border-bottom); lane 3 is the OVERVIEW STRIP (the whole-song lane at
// its one fixed tiny height, moved here from the bottom strip by commit B — its
// painter (paint_overview_strip), its press claim, its wheel band and its
// cursor cue all read top_overview_row_area below); lane 4 is the TRIM lane (the
// bar, its
// endcaps, every trim gesture the b/e chips used to carry, and the span-framing
// double-click — the one lane that also rides kTrimBarScalePercent, resting at
// 100 since the seventh glass ruling, through this same
// accumulation); lane 5 is the RULER lane (the timestamp ladder; its plain
// drag draws the REGION since 2026-08-12, the zoom entry deleted for good);
// lane 6 is the MARKER lane (the flags, their stems — pointer-inert since the
// seventh glass ruling — and the
// playhead's aliased head on the lane's bottom rows), whose bottom edge is
// flush with the waveform area top. (The trim and ruler lanes were ONE merged
// input band — top_trim_surface_area — for the trim surface arc's one day,
// 2026-08-11..12; the revert re-split them and deleted the accessor.)
GuiRect top_menu_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 0);
}

GuiRect top_tab_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 1);
}

GuiRect top_icon_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/true, 2);
}

GuiRect top_overview_row_area(const AppState& a) {
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

// THE BOTTOM STRIP IS ONE LANE — THE UNIFIED BOTTOM ROW (bottom lane 0;
// 2026-08-12, rows 8
// and 9 merged; the succession is at the bottom row's geometry block,
// render.h), resting on
// the WINDOW'S FOOT since commit B, with GAP 2's blank window ground between it
// and the waveform: the transport three on the left at the icon
// row's boxes with the monospace clock behind their separator (left-aligned
// since 2026-08-18), and a RIGHT-ANCHORED BLOCK of the four marker verbs with
// ADD TO SELECTION behind them, the
// marker-walk three and the four cardinal arrows, divided by two more of the
// ruled separators. (The arrows' four slots were a mode SWAP with the history
// companions from 2026-08-14 until the 2026-08-18 relayout took those four
// back to the icon row.) (The status chain moved into the TAB ROW on
// 2026-08-13; the OVERVIEW STRIP was bottom lane 0 under this row for
// the afternoon of 2026-08-12 and is TOP lane 3 now.) The dirty flag is
// the window title's dot, not a tenant here. THE LANE IS THE ICON ROW'S
// HEIGHT since 2026-08-14, its content and border both delegating to that
// row's accessors.
// bottom_row_area is the lane INCLUDING its 1px border-top (the waveform
// side — commit B's "thin border" above the row), as the strip stack allocates
// it; bottom_row_content_area is the
// ground under that border, the band every button, baseline and cell works
// in. Paint and hit agree through the one accessor exactly as the top rows'
// do through theirs.
//
// (The former pan-strip row retired earlier — pan lives on the plain-drag
// grab and the ctrl strip drag's horizontal axis.)
GuiRect bottom_row_area(const AppState& a) {
    return strip_row_rect(a, /*top_strip=*/false, 0);
}

GuiRect bottom_row_content_area(const AppState& a) {
    const GuiRect lane = bottom_row_area(a);
    const int b = bottom_row_border_h_px();
    return GuiRect{lane.x, lane.y + b, lane.w, lane.h - b};
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
    // The ONE right-wall owner. It was hoisted for the deleted strip drag's
    // per-event pan clamp, which had to derive the same wall the resting clamp
    // rests at; that caller left with the gesture (2026-08-15) and
    // clamp_viewport_start below is the one reader again. Degenerate branches: visible >= total
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
    // (authored_frame_at_column's source branch via displayed_grid_position_at_column)
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

// (THE STATUS CELL'S RECT IS DELETED — 2026-08-13, when the architect moved
// the whole status chain into the TAB ROW: "just put that text in the tab row,
// there's plenty of space there". The bottom row's middle-right span it named
// — from the clock's reserved cell to the arrow cluster's left edge — has no
// tenant left, so the span arithmetic, its degenerate-order widening and its
// two pre-first-paint fallbacks all went with it, and the painter's
// TransportLeft stash is read by the roster machinery alone again. The chain's
// damage owner is Viewport::invalidate_status_chain_area, which takes the tab
// row's LANE whole; the bottom row's remaining two owners are the clock cell
// below and Viewport::invalidate_modal_dialog_area's lane.)

// THE CLOCK'S RECT — the unified bottom row's reserved cell as the
// painter last drew it, in the row's LEFT BLOCK behind the transport's
// separator since 2026-08-18 (AppState::clock_cell_rect, whose stash contract
// is at the field). Narrow by construction: on_redraw clips to the damage
// region, so paint_bottom_strip runs but its buttons and its three separators
// fall outside the clip and cost nothing, which is what makes this affordable
// at the pre-paint hook's per-frame cadence. THE MOVE MADE IT NO WIDER AND NO
// NARROWER — the cell's width is the same shaped specimen and only its origin
// changed — so nothing about this owner or its consumers moved with it. (The row's STATUS CHAIN was the third
// such tenant until 2026-08-13, when it moved into the TAB ROW — see the
// deleted status cell's record just above; this lane carries no chain now.)
//
// BEFORE THE ROW'S FIRST PAINT the stash is zero and the answer is the WHOLE
// lane — the honest widening, and unreachable in practice: the first frame
// damages the window entire. THE MODAL STATE READS THE SAME ZERO (2026-08-13):
// the row's painter zeroes the cell while it yields to a prompt or a dialog
// editor, so a clock-moving route during a modal widens to the lane instead of
// damaging a cell that is not being painted.
GuiRect clock_invalidate_rect(const AppState& a) {
    const GuiRect cell = a.clock_cell_rect;
    if (cell.w <= 0 || cell.h <= 0) return bottom_row_area(a);
    return cell;
}


// THE LINUX ENTRY POINT and nothing more: the argument check, then the one GUI
// body (gui_main.h). Android's backend has its own entry (android_main) and
// calls the same body with no argument, so this wrapper is compiled out of that
// build.
#ifndef __ANDROID__
int main(int argc, char** argv) {
    // THE ARGUMENT IS OPTIONAL (2026-08-27, the project model): with none the
    // app opens the device config's last project, or the first valid one in
    // name order; with one it must be a project's SOURCE under the config's
    // projects path (startup_source, project_model.h). There is no blank
    // window to load into either way — the app always has a project open.
    if (argc > 2) {
        std::fprintf(stderr, "Usage: warptempo_gui [<project source .wav>]\n");
        return 1;
    }
    return gui_main(argc == 2 ? argv[1] : nullptr);
}
#endif

namespace {

// What one project's session hands back to the loop: the process exit status
// when the session ends the process, and otherwise the NAME of the project to
// reopen (empty = exit with `exit_status`).
struct GuiProjectOutcome {
    int         exit_status = 0;
    std::string reopen;
};

// ONE PROJECT'S SESSION — everything that is ONE PER PROJECT, built around
// `project`'s source, run, and torn down before this returns (the loop
// contract is at platform.h; gui_main below is the loop). The window, the
// input core, the device config and the render cache are the caller's and
// outlive every call.
GuiProjectOutcome run_project(GuiPlatform&            gui,
                              DeviceConfig&           device_config,
                              RenderCache&            render_cache,
                              const GuiProjectSource& project,
                              bool&                   window_up) {
    AppState     app;
    GuiAudio     audio;
    GuiPlayback  playback;
    WaveformCache wf_cache;
    // Top-strip flag rects live on their own surface, rebuilt
    // synchronously from on_tick. Constructed alongside wf_cache so they share
    // the same lifetime; passed by reference into GuiPaintHandler. (Trim has no
    // cache — every trim pixel paints live per frame in
    // GuiPaintHandler::paint_trim; marker stems are the
    // live overlay paint_marker_stems, off this cache's own published stash.)
    FlagCache     flag_cache;

    // THE DEVICE CONFIG'S LIVE VALUES, mirrored into this project's AppState
    // from the loop's one struct, and the struct itself seated by pointer:
    // the two editable keys keep their AppState fields (their readers did
    // not move), and every commit writes both the field and the struct, then
    // the file (the callers inventory at write_device_config, device_config.h).
    app.device_config = &device_config;
    app.gui_scale     = device_config.gui_scale;
    app.projects_repo = device_config.projects_repo;

    // THE WINDOW, ONCE PER PROCESS: the first project's session opens it and
    // every later one inherits it standing (the loop contract, platform.h).
    // It is here rather than ahead of the loop because init() takes
    // AppState's cold size, which is a per-project object's — and the scale
    // and the touch slop are already installed by gui_main by the time the
    // first session runs, so the first configure and the first painted frame
    // are at the user's scale as before.
    if (!window_up) {
        if (!gui.init(app.width, app.height, "warptempo_gui")) {
            return {1, {}};
        }
        window_up = true;
    }

    // -- Viewport + invalidation helpers ------------------------------------
    //
    // The viewport-mutation and invalidation helpers are methods on the
    // Viewport struct (viewport.{cpp,h}), including the tab row's
    // invalidate_status_chain_area and the bottom row's own two,
    // invalidate_clock_area and invalidate_modal_dialog_area. Every other
    // cross-cutting operation is a method on its owning struct constructed
    // below — stop_playback_if_playing / toggle_playback
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
        return {1, {}};
    }
    // Waveform-cache rebuild runs on this dedicated worker; the
    // paint thread becomes blit-only. Must be constructed before
    // GuiPaintHandler (which takes it as a reference).
    GuiWaveformWorker waveform_worker;
    if (!waveform_worker.init()) {
        std::fprintf(stderr,
            "warptempo_gui: Failed to start waveform worker; exiting\n");
        return {1, {}};
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
        return {1, {}};
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
        return {1, {}};
    }
    // THE SYNCHRONIZATION WORKER (2026-08-27): the File menu's Synchronize to
    // external storage act mirrors the project's renders onto the mounted
    // removable volume here instead of on the GUI thread, which a USB write
    // would freeze for seconds. Single act in flight, completion delivered
    // through its own eventfd below, and shutdown() JOINS an act in progress at
    // the session's tail (the copies are already the user's intent). Fatal on a
    // failed init like its four siblings.
    GuiExternalSyncWorker external_sync_worker;
    if (!external_sync_worker.init()) {
        std::fprintf(stderr,
            "warptempo_gui: Failed to start the synchronization worker; "
            "exiting\n");
        return {1, {}};
    }
    // THE RENDER CACHE IS THE CALLER'S — the one per-process instance gui_main
    // constructs, inits and shuts down around the whole project loop (its
    // reasoning is there): target-view reuse, the archival reuse/publish
    // rungs and the loaded-in-place render's survival after the batch
    // folder is wiped all read it through target_render, which holds it by
    // reference and is constructed just below. A failed init() there leaves
    // the cache disabled (every lookup misses), so target_render needs no
    // special-casing.
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
    // THE A/B AUDITION (2026-08-26): after active_views and target_render,
    // both of which it holds; before the input handler, which holds it. Its
    // advance is called from the tick's natural-end branch below.
    GuiAbAudition ab_audition(app, audio, playback, playback_lifecycle,
                              active_views, target_render);
    Undo undo(app, viewport, selection, playback_lifecycle, active_views,
              target_render);
    GuiPhaseResetMarkersOps phase_resets(app, audio, viewport, selection, undo,
                                         playback_lifecycle, target_render);
    GuiWarpMarkersOps warpops(app, audio, viewport, selection, undo,
                              playback_lifecycle, target_render);
    MarkerDragOps marker_drag(app, audio, viewport, undo, target_render);
    GuiFlagEditor flag_editor(app, audio, viewport, selection, undo,
                              target_render);
    GuiRendersDir renders_dir(app);
    // THE RENDER PLAYER (2026-08-28): after renders_dir and target_render,
    // both of which it holds; before the input handler, which holds it. Its
    // tick is the on_tick fork below, its buffer dies with this AppState
    // (after playback.shutdown at the session tail, so the engine never
    // outlives what it is bound to).
    GuiRenderPlayer render_player(app, audio, gui, playback, playback_lifecycle,
                                  viewport, target_render, renders_dir);
    // The stop body's back-pointer onto the player (the field's own comment,
    // playback_lifecycle.h): the one stop body publishes the head unit's
    // "paused" from its player fork, and the player is built after it.
    playback_lifecycle.render_player = &render_player;
    PhaseResetPropagate phase_reset_propagate(app, viewport, undo,
                                              target_render, active_views,
                                              playback_lifecycle);
    GuiSaveOps save_ops(app, undo, active_views);
    GuiPrompt prompt(app, gui, viewport,
                     phase_reset_propagate, save_ops, playback_lifecycle,
                     render_player);
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
    gui.set_sync_worker_completion_fd(external_sync_worker.completion_fd(),
        [&external_sync_worker]() {
            external_sync_worker.on_completion_event();
        });
    GuiInputHandler input_handler(app, audio, gui, playback,
                                  viewport, selection, undo,
                                  warpops, phase_resets, marker_drag,
                                  flag_editor,
                                  renders_dir, active_views, ab_audition,
                                  render_player,
                                  phase_reset_propagate,
                                  async_renderer,
                                  history_commit_worker,
                                  history_prefetch,
                                  external_sync_worker,
                                  playback_lifecycle, save_ops, prompt,
                                  settings_editor, target_render,
                                  paint_handler);
    // Back-wire the settings editor to the input handler (constructed after the
    // editor, which the input handler holds by reference — the cycle is
    // resolved with a pointer set here). The editor reaches
    // handle_active_audio_view_toggle / apply_gui_scale / commit_trim_mutation
    // through it, so a `:`-typed GUI key funnels into the same gesture code.
    settings_editor.input = &input_handler;
    // The prompt's back-pointer, for the render player's load confirmation
    // (its one reader is recorded at the member, prompt.h).
    prompt.input = &input_handler;
    // And the render player's, for the ring clear a car command owes before
    // it synthesizes a key (its one reader is recorded at the member,
    // render_player.h).
    render_player.input = &input_handler;
    // And the A/B audition's, for the `c` command each half opens with (its
    // one reader is apply_working_zoom; the rule is at ab_audition.h).
    ab_audition.input = &input_handler;
    // Same back-wire for the phase-reset propagate: its paste tail lands in
    // target view through switch_active_audio_view_to, the chokepoint that
    // lives on the input handler (constructed after the propagate, which the
    // input handler holds by reference — the cycle is resolved with this
    // pointer set).
    phase_reset_propagate.input = &input_handler;
    // And Undo's, for the same chokepoint: a restore puts the reader back in the
    // authoring view the entry recorded, and the S/T axis of it is the input
    // handler's (the other two are GuiActiveViews', which Undo holds outright).
    // Its one reader is recorded at the member, undo.h.
    undo.input = &input_handler;
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
    // cursor lock (pointer-constraints + relative-pointer). ONE CLIENT — the
    // one nav drag (pan by default, zoom while ctrl is held), for infinite
    // pan/zoom travel: the overview lane's ctrl strip drag was the second and
    // was deleted whole on 2026-08-15, and the lane's surviving three gestures
    // are absolute and capture-free.
    // All the platform methods self-guard (begin no-ops when a capture is live
    // or the compositor lacks the managers; end is idempotent; the restore
    // riders no-op uncaptured), so the input layer
    // stays agnostic to whether capture is available. The begin hook forwards the
    // gesture's own cursor kind, which is what the release restores (contract at
    // GuiPlatform::begin_pointer_capture); the nav drag's mid-gesture mode
    // switches ride the restore-x clear, the restore-kind re-stamp, the
    // lateral freeze that stops the zoom phase's discarded sideways travel
    // moving the pointer's notional position, and the ctrl-up handover that
    // gives that position the stem's own column before the override is
    // dropped (2026-08-14, the live-ctrl model). The WRAP SPAN rides the
    // capture's begin instead of a mode switch — it belongs to the captured
    // pointer rather than to a phase.
    input_handler.begin_strip_pointer_capture = [&](GuiCursorKind restore_kind) {
        gui.begin_pointer_capture(restore_kind);
    };
    input_handler.end_strip_pointer_capture   = [&]() { gui.end_pointer_capture(); };
    input_handler.set_strip_capture_restore_x = [&](double sx) { gui.set_capture_restore_x(sx); };
    input_handler.clear_strip_capture_restore_x =
        [&]() { gui.clear_capture_restore_x(); };
    input_handler.set_strip_capture_restore_kind =
        [&](GuiCursorKind kind) { gui.set_capture_restore_kind(kind); };
    input_handler.set_strip_capture_notional_x_frozen =
        [&](bool frozen) { gui.set_notional_x_frozen(frozen); };
    input_handler.set_strip_capture_notional_x =
        [&](double sx) { gui.set_notional_pointer_x(sx); };
    input_handler.set_strip_capture_wrap_span =
        [&](double lo, double hi) { gui.set_capture_wrap_span(lo, hi); };

    // The touch navigation (touch phase 1, 2026-08-11; SIX hooks since
    // pan-primary's touch half, the eighth glass ruling 2026-08-12): the
    // platform's nav frames —
    // the two-finger PINCH ZOOM (zoom only since 2026-08-14: two fingers
    // never pan, the nav body discarding their centroid delta), and the
    // phone model's
    // single-finger pan frames born of a drag starting on the pan surface —
    // drive the input handler's ONE touch-nav body, which runs the
    // strip-drag family's own viewport chokepoint — the
    // set_keyboard_intent_cancel_hook wiring precedent, one narrow
    // platform-to-GUI hook set. The PAN-ZONE QUERY is the third hook: the
    // platform asks it once at each first finger's down, and the GUI answers
    // the NAVIGATION SURFACE — upper waveform half (whole in the `h` view) +
    // ruler + the marker lane minus its flag boxes — surface geometry only
    // (refusals stay per-frame in the update body and in the region begin).
    // THE THIN-LANE QUERY is its twin, asked at the same down: the GUI answers
    // whether the point lies on the overview strip or the trim bar — lanes too
    // small and too precise to hold a nav gesture — and the bit refuses at TWO
    // doors, the platform's own second-finger fork and the GUI's per-frame
    // refusal, so once one finger is down on such a lane the second is
    // completely ignored and no nav gesture ever runs there (2026-08-15).
    // The REGION TRIO is the eighth ruling's half: the zone's stretched
    // window (the region-hold beat) expires into the region former —
    // begin at the down point, one update per frame, an end that always
    // fires — the dead trim-move hooks' exact pattern reborn (contracts at
    // GuiPlatform::set_touch_nav_hooks; the GUI bodies at
    // begin_touch_region's declaration). A one-finger gesture off the zone
    // needs no wiring: it
    // is translated into the ordinary pointer deliveries above, and nothing
    // on this side can tell which device produced them. Contracts at
    // GuiPlatform::set_touch_nav_hooks (the platform half) and at
    // apply_touch_nav_update's declaration (the GUI half, including why the
    // nav gesture stops short of the strip drag's pointer-press arm).
    gui.set_touch_nav_hooks(
        [&](const GuiTouchNavFrame& frame) {
            input_handler.apply_touch_nav_update(frame);
        },
        [&]() { input_handler.end_touch_nav(); },
        [&](int x, int y) {
            return input_handler.touch_point_in_pan_zone(x, y);
        },
        [&](int x, int y) {
            return input_handler.touch_point_on_thin_lane(x, y);
        },
        [&](int x, int y) { input_handler.begin_touch_region(x, y); },
        [&](int x, int y) { input_handler.update_touch_region(x, y); },
        [&]() { input_handler.end_touch_region(); });

    auto invalidate_modal_dialog_area = [&]() { viewport.invalidate_modal_dialog_area(); };
    auto invalidate_clock_area       = [&]() { viewport.invalidate_clock_area(); };
    auto invalidate_playhead_columns = [&](double a, double b) { viewport.invalidate_playhead_columns(a, b); };
    auto follow_scroll_if_needed     = [&]() { viewport.follow_scroll_if_needed(); };

    bool initial_load_done = false;

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
        // A PHYSICAL KEY ARRIVAL ENDS A HELD ARROW BUTTON'S REPEAT BURST, and
        // THIS hook is that edge: it sees exactly the platform's key
        // deliveries and none of the synthetic on_key entries (the chrome
        // lift, the dropdown item, and the burst's own tick fires — which
        // must not end the schedule they ride, so the disarm cannot live at
        // on_key's own top). It is LOAD-BEARING FOR UNDO rather than
        // hand-feel: Undo::coalesce_gesture merges a synthesized repeat by
        // KIND ALONE, with no subject test, on the premise that no command
        // can run between a burst's opener and the repeats behind it — and
        // under press-time dispatch the PRESS edge alone spans that premise:
        // a key RELEASE runs no command (the one release act, the modal
        // Enter/Space commit, needs its own arming press, which this line
        // already caught — and a prompt standing disarms the burst anyway,
        // through the tick's per-fire repeat_eligible re-ask). Only the ARM's
        // schedule dies: the arm itself is the pointer's and a key press does
        // not end a finger's hold, so the lift still runs the act the burst
        // had not yet suppressed. A synthesized KEY repeat cannot arrive
        // under a held button at all — the platform kills its own key hold at
        // any pointer-button press — so no repeat-bit test is needed. The
        // burst's full edge inventory is at AppState::ChromePress.
        app.chrome_press.repeat_due_ms = 0;
        input_handler.on_key(key, mods);
    });

    // THE KEY RELEASE, this product's one act-on-lift keyboard edge: bare Enter
    // or bare Space on a focused modal dialog button commits it here (the
    // contract is at GuiInputHandler::on_key_release). Every other release
    // resolves nothing.
    gui.set_on_key_release([&](GuiKey key) {
        input_handler.on_key_release(key);
    });

    // THE CAR'S BUTTONS (design §3): each command the platform drained is the
    // render player's to translate into its own keys (the contract at
    // GuiRenderPlayer::on_media_command). Installed per project like every
    // other handler, and — unlike them — CLEARED at the session tail, so a
    // button pressed between two projects is dropped by the platform's null
    // test rather than delivered against a dead set (the hook captures this
    // session's player).
    gui.set_on_media_command([&](GuiMediaCommand cmd) {
        render_player.on_media_command(cmd);
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
        // THE RENDER PLAYER, THE PICKER AND EVERY STANDING MODAL EDITOR GO
        // DOWN INSIDE request_close, not here: the close road owns those
        // steps for Ctrl+Q and for this callback alike, so neither a mode
        // nor an editor — nor either overlay band — is left standing under
        // the unsaved-work prompt.
        prompt.request_close(GuiCloseTarget::Exit);
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
    // EFFECT LIST for the hook — tooltip hide, the armed chrome press, the
    // modal dialog's armed button, the popup's two
    // item faces plus its press claim, and the PAIR that a leave through row 1
    // skips, the roster hover clear and the menu-row disarm (which is itself
    // gated a second time, on no menu being open). The platform-side sites name
    // their OWN concern and point
    // here rather than each keeping a list that can drift (the setter contract
    // and the member comment in input_core.h, and the capability-loss fire
    // site in input_core.cpp).
    // THE TWO EDGES ARE NOT THE SAME EDGE (codex 2026-08-03) — and SINCE
    // 2026-08-08 THE BODY IS TOLD WHICH ONE IT IS, the platform handing in a
    // GuiPointerLeaveReason, because one effect below now differs between them.
    // (SINCE TOUCH PHASE 1, 2026-08-11, a touch pointer translation's end
    // fires this hook too — as OrdinaryLeave, and ONLY on its no-focus arm
    // since codex round 3: with the physical pointer focused, the platform
    // delivers a restore MOTION at the mouse's own position instead and this
    // body never runs — the ordinary motion path re-derives hover and the
    // settled cursor from truth (the fork's one statement is at
    // deliver_touch_translation_end, input_core.cpp). On the arm that
    // DOES fire, the body needed no change:
    // clearing hover faces where a finger last was is precisely what the
    // no-hover-under-touch consequence asks for, and the row-1 keep below
    // reads the remembered position exactly as it does for a mouse. The fire
    // sites are the touch edge inventory's, input_core.h.
    // ONE OF THOSE FIRINGS IS THE SECOND-FINGER UPGRADE, whose end is the
    // ABNORMAL one (codex round 19) — no release is delivered, so the three
    // press ARMS this body drops are what stops a pinch dispatching whatever
    // the held finger was resting on. The clears were already exactly right
    // for it; only their load-bearingness grew.)
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
    // THE ARMED CHROME PRESS joins it, and since the act moved to the release
    // (2026-08-13) this is sharper than a face: the arm is a pending ACT, and
    // this is the BUTTON-LOST edge — the hold stops being the pointer's to
    // show and the act must not wait on a release that may never come. A
    // release that does arrive later (the ordinary-leave case) finds no arm
    // and dispatches nothing: the harmless no-op named above, and the intended
    // answer — a release outside the window's visit runs nothing.
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
    // button KEEPS ITS FACE — a menu button stays lit under a pointer resting
    // on the
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
        // THE THREE RELEASE-TIME ARMS. This hook is no longer their only end
        // (codex round 20): they also die at the BUTTON-LOST edge, an unheld
        // motion while one of them stands — which is what the touch upgrade's
        // abnormal end delivers, and what a lost physical button delivers too
        // (clear_release_time_press_arms). The calls stay spelled out here
        // rather than routed through that owner because THIS edge asks a
        // different question and clears more with it: a pointer that has left
        // is on no button AND at no position, so the hover faces, the menu
        // row's mode and the tooltip go too, and the popup's HOVERED item goes
        // whether or not anything was armed. The narrower owner is a subset of
        // this body, deliberately.
        input_handler.clear_redesign_button_press();
        // The MODAL's armed dialog button goes on the same edge and for a
        // sharper reason than the roster's face: that arm is an act that has
        // not happened yet, and the pointer is on no button now (the contract
        // is at clear_modal_dialog_press).
        input_handler.clear_modal_dialog_press();
        input_handler.clear_dropdown_pointer_state();
        // THE RENDER PLAYER'S TWO ARMS go on the same edge (2026-08-28) — the
        // overlay's row press and the scrub's marker drag, both acts that
        // have not happened yet and both dropped uncommitted here exactly as
        // at the button-lost edge (clear_release_time_press_arms).
        input_handler.clear_folder_overlay_press();
        // AND THE BAND'S HOVER FACE, on the hover half of this hook's own
        // question: a pointer that has left is on no row, and no motion will
        // ever arrive to say so.
        input_handler.clear_folder_overlay_hover();
        input_handler.clear_player_scrub_drag();
        // AND THE SCRUB HANDLE'S HOVERED OUTLINE, the same hover half of the
        // question one surface over: the handle's accent is re-answered at
        // PAINT from the remembered position and the in-window flag (which
        // this hook has just cleared), so what it owes is the damage — the
        // cell, while the player owns the row.
        if (app.render_player.active && app.modal_dialog.valid &&
            app.modal_dialog.scrub.w > 0 && app.modal_dialog.scrub.h > 0) {
            viewport.invalidate_rect(app.modal_dialog.scrub);
        }
    });

    // WINDOW-ACTIVATION EDGE -> the redesigned header's ground swap. The hook
    // fires only when the xdg_toplevel state actually flips (the platform owns
    // the edge test), so this is a mirror-and-damage pair with no comparison of
    // its own. It takes the pointer-leave hook's shape for the pointer-leave
    // hook's reason: a protocol edge that changes what should be on screen and
    // carries no other event to repaint it.
    // TOP-STRIP damage is the exact rect — rows 1 and 2 read the flag for
    // their ground, and both live there — PLUS THE MODAL ROW WHILE THE RENDER
    // PLAYER STANDS (2026-08-28): the play-scrub is the flag's THIRD reader
    // and the only one below the waveform, its played groove taking the
    // focused blue or the dimmed one (architect R29). THE READERS ARE THREE
    // AND THE DAMAGE IS TWO RECTS, re-grepped at this line rather than
    // inherited; the row's rect is spent only while the player owns it,
    // because that is the only state in which anything down there reads the
    // flag at all.
    // THE MIRROR IS SEEDED HERE AND KEPT BY THE HOOK, and the seed is what a
    // REOPEN needs (2026-08-28): the loop builds a FRESH AppState per project
    // (gui_main's contract, platform.h) whose window_activated is born false,
    // while the platform's own bit is already true and fires NO edge for a
    // focus that never changed — so without this line the reopened session
    // painted rows 1 and 2 unfocused until the next real focus flip. It is one
    // site for both backends, beside the hook rather than inside it, and it
    // needs no damage of its own: the whole window is invalidated below,
    // before run(). In the FIRST session it reads the cold false and changes
    // nothing. The GEOMETRY takes the same shape one hook further down, where
    // redeliver_geometry() re-fires on_resize for a size that did not change.
    app.window_activated = gui.window_activated();
    gui.set_activation_changed_hook([&] {
        app.window_activated = gui.window_activated();
        viewport.invalidate_top_strip();
        if (app.render_player.active) viewport.invalidate_modal_dialog_area();
    });

    // THE PLATFORM'S CONSUMED KEYBOARD EDGES (codex round 4, 2026-08-11):
    // keyboard leave / keyboard-capability loss and every Super-swallowed
    // press are key events the GUI's own chokepoints can never see — the
    // platform consumes them without calling on_key — so the platform reports
    // them through this one hook instead of the application growing a second,
    // partial list. The fire classes and the per-swallowed-delivery decision
    // are at the setter's contract (input_core.h). THIS BODY IS THE
    // AUTHORITATIVE EFFECT LIST for the hook, the pointer-leave hook's own
    // model, and it holds ONE application-side key intent: THE MODAL DIALOG'S
    // KEYBOARD PRESS ARM, which is sharper than a face — that button is
    // painted down waiting for a RELEASE that these edges guarantee will never
    // be delivered, so the arm is dropped and the box damaged
    // (clear_modal_dialog_key_press).
    // THE MEMBER IS A KEY INTENT, which is the list's membership rule and the
    // reason THE ARROW BUTTONS' HOLD-REPEAT IS NOT HERE (2026-08-16, where
    // its pre-2026-08-13 form was): that burst hangs off the armed CHROME
    // PRESS, pointer intent, and no edge of this hook ends a finger's hold on a
    // button (the reasoning is at the setter's contract, the burst's whole edge
    // inventory at AppState::ChromePress).
    gui.set_keyboard_intent_cancel_hook([&] {
        input_handler.clear_modal_dialog_key_press();
    });

    // THE SETTLED BOUNDARY AND ITS THREE CONSUMERS (architect 2026-08-03,
    // replacing the per-site model; the third joined 2026-08-14 with the nav
    // drag's live ctrl). The run loop fires this at the TAIL of every
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
    // derivation had missed while whole classes (a keyboard zoom moving the trim
    // endcaps under a resting pointer, the zoom and navigation keys, the trim
    // keys, an
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
    // THE CONSUMERS ARE INDEPENDENT, so the order here is free: the zone map
    // refuses every cue while a popup is open and reads neither item face, and
    // the recompute writes nothing the map reads.
    // NEITHER OF THOSE TWO RE-LIGHTS WHAT THE POINTER-LEAVE HOOK ABOVE DROPPED:
    // each refuses on app.pointer_in_window inside its own body, so a
    // per-iteration call cannot resurrect a face from coordinates the pointer
    // has left behind.
    //
    // THE NAV DRAG'S ZOOM/PAN MODE IS THE THIRD CONSUMER (2026-08-14), and it
    // is the same disease once more: ctrl SELECTS what a live navigation drag
    // means, so releasing it under a MOTIONLESS pointer must drop the zoom stem
    // and re-stamp the capture's restore there and then — the old model synced
    // in on_motion alone, so the stem stood until the mouse moved again
    // (architect, from the rig). It refuses immediately with no nav drag
    // standing, and unlike the two above it is deliberately NOT gated on the
    // pointer being in the window: a captured drag's pointer is virtual and
    // may be anywhere, and the gesture is what owns the answer. It is
    // independent of both — the map refuses every cue while a gesture is live,
    // and the hover walk reads nothing it writes — so the order here stays
    // free.
    gui.set_loop_settled_hook([&](GuiInputState mods) {
        input_handler.refresh_pointer_cursor(mods);
        input_handler.recompute_dropdown_hover(mods);
        input_handler.sync_nav_drag_mode(mods);
    });

    gui.set_on_motion([&](int mouse_x, int mouse_y, GuiInputState mods) {
        input_handler.on_motion(mouse_x, mouse_y, mods);
    });

    // -- File loading --------------------------------------------------------
    //
    // load_file (file_loader.{h,cpp}) is the sole loader, invoked once from
    // the startup tick below. The GUI has no in-session file open or
    // drag-and-drop INTO A STANDING SET: the source is fixed for this
    // session's whole life, and File → Open project reopens by ending this session
    // and starting the next around the chosen source (gui_main's loop), so
    // there is nothing to wire here.

    // Tick: runs once per event-loop iteration. During playback, snapshots
    // the audio thread's cursor and mirrors it into the main-thread playhead,
    // invalidating just the columns and timestamp that changed. Also
    // detects natural end-of-playback via the atomic playing flag.
    gui.set_on_tick([&]() {
        // Startup file load, deferred out of pre-run() so the window maps and
        // paints first (the compositor's initial configure / first frame only
        // land once run() is pumping). Gated on has_initial_configure() so the
        // load — and its loading notice — run against a mapped, painted surface.
        if (!initial_load_done && gui.has_initial_configure()) {
            initial_load_done = true;
            // THE WHOLE RESOLVED PROJECT, not just its source path: the load
            // takes the project's NAME from here rather than deriving one from
            // the source's parent folder (the rule is at
            // GuiFileLoader::load_file).
            if (!file_loader.load_file(project)) {
                // A project is opened by rebuilding this whole object set
                // around it and there is no in-session replacement surface,
                // so every load refusal is terminal. It is also the
                // ADVERSARIAL class by construction: the Open project picker
                // ran the load's own failure arms before anything was torn down
                // (source_load_dry_run, file_loader.h), and startup resolved
                // the folder the same way, so a refusal here means the disk
                // changed between that check and this load. Deeper
                // decode/sidecar failures already request exit at the owning
                // site; request_exit() is idempotent for those paths.
                gui.request_exit();
                return;
            }
            // THE REMEMBERED PROJECT (2026-08-27): every successful open —
            // startup's included — writes its name into the device config's
            // last_project, the fourth writer of that file. Gated on a real
            // change like the other three, so a relaunch of the same project
            // rewrites nothing.
            if (device_config.last_project != project.name) {
                device_config.last_project = project.name;
                (void)write_device_config(device_config);
            }
            // THE PREFETCH'S STARTUP KICK (2026-08-07), here because this is
            // where the source settles: app.source_audio_path is final by now
            // (the sidecars applied above) and app.projects_repo is the
            // device config's, and the scan needs exactly those two. It costs
            // the GUI thread one queue push — everything else happens on the
            // worker while the user is still looking at the first frame.
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
            // owner: a top-row tooltip hangs BELOW the top strip, a BOTTOM-ROW
            // one hangs ABOVE its lane, the painter's own flip — and that
            // second arm covers both of the row's surfaces, its seventeen
            // roster buttons (the transport three, and the right block's four
            // marker verbs with the Edit flag button, the Marker Measure and
            // Add to Selection behind them, three walk steps and four cardinal
            // arrows) and the MODAL's own buttons
            // (2026-08-13), which paint in the same lane. The HIDE edge has the
            // published rect and damages exactly that.
            const AppState::RedesignTooltip::Owner tip_owner =
                app.redesign_tooltip.owner;
            const bool tip_on_bottom_row =
                tip_owner.index >= 0 &&
                (tip_owner.surface ==
                     AppState::RedesignTooltip::Surface::Dialog ||
                 redesign_button_in_transport_row(
                     static_cast<RedesignButton>(tip_owner.index)));
            if (tip_on_bottom_row) {
                const GuiRect tr = bottom_row_area(app);
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
            // The bottom row's damage fork, here too (2026-08-11, with the
            // transport row): a drifting bottom-row face damages its own
            // bottom-row lane, everything else the top
            // strip. Each strip pays only for its own drift — the walk keeps
            // going until both verdicts are known (or the roster ends).
            //
            // THE STASH IT COMPARES AGAINST IS AS-PAINTED, NOT AS-COMPUTED
            // (2026-08-15, the transport-pair staleness fix — the mechanism
            // and its record live at publish_button_face, paint_handler.cpp):
            // a row painter runs whole on any damage intersecting its lane,
            // but the publisher refreshes a face's enabled/selected bits only
            // when the current clip covers that button's pixels. Without that
            // gate a NARROW lane damage in the same frame as the state edge —
            // the click act's stop damaging the clock cell, the natural
            // end-of-song teardown doing the same — stamped the stash live
            // under a clip that never redrew the buttons, so this comparator
            // saw stash equal to live forever and the play/stop pair stayed
            // painted stale until an unrelated full-lane damage (a hover)
            // repaired it. With the gate, the drift survives the masking
            // frame, this walk catches it on the next tick, and the
            // full-strip damage below is what both repaints the pixels and
            // republishes the stash — one pass, the comparator's own
            // documented model, now true. (The transport's state was the
            // ENABLED bit when that fix landed, the SELECTED bit under the
            // same day's radio ruling, and is the GLYPH since the collapse of
            // play and stop into one button — this comparator reads all three,
            // so the repair it describes carried across both moves untouched.)
            //
            // THE GLYPH TERM IS THE THIRD AND IT CLOSED A LATENT CASE WITH IT
            // (2026-08-15): a stateful GLYPH changes a button's pixels without
            // moving either of the other two bits, so this walk was blind to
            // one. Four buttons have a second glyph — Save, Render, the
            // read-only toggle and the collapsed play/stop button — and only
            // the read-only toggle's rode a bit already stashed (its lamp).
            // RENDER'S mid-render Cancel face is the one that was quietly
            // uncovered before this term: render_cancel_face moves neither
            // enabled nor selected, so its repaint rested on the render
            // routes' own damage alone.
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
                    f.selected != redesign_button_selected(app, id) ||
                    f.glyph_swapped !=
                        redesign_button_glyph_swapped(app, id);
                if (!drifted) continue;
                if (redesign_button_in_transport_row(id))
                    drift_transport = true;
                else
                    drift_top = true;
            }
            if (drift_top) invalidate_top_strip();
            if (drift_transport)
                viewport.invalidate_rect(bottom_row_area(app));
        }

        // THE ON-SCREEN KEYBOARD'S SHOW AND HIDE (2026-08-27), the roster
        // comparator's own mechanism applied to a whole surface rather than to
        // a face. The keyboard appears and disappears with the EDITORS'
        // open and close, and those routes damage the marker lane or the bottom
        // row — never the band this surface paints in, which is the waveform
        // area's lower part. So the live answer is compared against the
        // as-painted bit, which the painter refreshes only on a frame whose
        // rect FULLY COVERS that band (the roster publisher's own rule, argued
        // at the painter): every other frame leaves the drift standing, and
        // this is where it is paid.
        //
        // THE DAMAGE IS BOTH RECTS ON EITHER EDGE, and the reason is the
        // discrete-command rule: on the HIDE the waveform under the surface has
        // to come back whole, and on the SHOW the surface's own band has to be
        // covered — the band overhangs the waveform's bottom into gap 2, so
        // neither rect contains the other. Two cheap calls the platform
        // coalesces, exactly as the tooltip's show edge pays.
        //
        // It costs one platform query and one integer compare per tick with the
        // surface down, which on the laptop is its permanent state.
        //
        // THE SHOW AND HIDE ARE NOT THE WHOLE OF IT: this comparator watches
        // STANDING and nothing else, and the live editor SESSION can change
        // without it moving — a flag-editor retarget, or a close and a reopen
        // inside one drained batch. That change has its own owner and IS NOT
        // CALLED HERE: a tick with no paint behind it has nothing to correct on
        // screen, and every frame that does paint runs the owner in the
        // PRE-PAINT hook below, which is strictly earlier and strictly more
        // often than this (the caller list is at reconcile_session).
        // THE SLOT HAS TWO TENANTS SINCE 2026-08-28 (the folder overlay
        // replaces the keyboard in the same band while the render player
        // stands), so the live answer is the OR of the two standing
        // predicates against the ONE as-painted bit the slot's paint dispatch
        // writes (AppState::keyboard_slot_painted_standing).
        {
            const bool slot_live = onscreen_keyboard::stands(app, gui) ||
                                   folder_overlay::stands(app);
            if (slot_live != app.keyboard_slot_painted_standing) {
                viewport.invalidate_waveform_area();
                // THE SLOT'S BAND AT ITS TALLEST, not either tenant's own:
                // on the HIDE the rect has to erase a surface that no longer
                // stands (so there is nothing to ask its height of), and the
                // overlay's band is as tall as its listing while the
                // keyboard's is its four key rows — one rect that contains
                // both (onscreen_keyboard::slot_damage_rect).
                viewport.invalidate_rect(
                    onscreen_keyboard::slot_damage_rect(app));
            }
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

        // THE CHROME BUTTON HOLD-REPEAT (architect 2026-08-16): while a press
        // stands on one of the bottom row's four cardinal arrows, this
        // synthesizes its chord on the keyboard's own cadence — a hold beat,
        // then the compositor's advertised repeat rate — stamped as a repeat so
        // the undo coalescing is the held key's own rule. One kind compare when
        // idle; every firing condition (the arm, the schedule, the pointer on
        // the button, the enabled bit, the eligibility) lives in the body.
        // Deliberately NOT gated on any_pointer_gesture_active, unlike the
        // hover recompute above: the held button IS a live pointer act, and the
        // rows' presses arm no gesture that predicate names.
        input_handler.tick_chrome_press_repeat();

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
        //
        // A DIALOG EDITOR WHOSE FIELD HAS LOST THE FOCUS RUNS NO BLINK
        // (2026-08-13, the same ruling that stopped the caret PAINTING there —
        // "the blinking caret, the I-beam, continues to blink in the text field
        // even though it has lost focus"). One term, the ring's own -1, so the
        // tick and the painter cannot disagree about whether there is a caret;
        // without it the loop would keep waking twice a second to damage a lane
        // whose caret nobody draws. THE TOP-STRIP FLAG EDITOR IS OUTSIDE IT and
        // must be: it is not a dialog, it has no ring, and the focus index it
        // would be reading belongs to whatever dialog is up over it.
        const bool dialog_field_focused = app.modal_dialog_focus < 0;
        if (text_editor::is_active(app.top_flag_editor)) {
            const bool is_dialog =
                app.top_flag_editor.kind == text_editor::Kind::BpmBracket;
            const bool now_visible =
                (!is_dialog || dialog_field_focused) &&
                text_editor::cursor_visible_now(app.top_flag_editor);
            if (now_visible != app.top_flag_editor_blink_last) {
                app.top_flag_editor_blink_last = now_visible;
                if (is_dialog)
                    invalidate_modal_dialog_area();
                else
                    invalidate_top_strip();
            }
        }
        // Same shape for the settings prompt (a dialog editor); the
        // modal's own owner is the bottom row's lane.
        if (text_editor::is_active(app.settings_editor)) {
            const bool now_visible =
                dialog_field_focused &&
                text_editor::cursor_visible_now(app.settings_editor);
            if (now_visible != app.settings_editor_blink_last) {
                app.settings_editor_blink_last = now_visible;
                invalidate_modal_dialog_area();
            }
        }
        // And for the history view's commit-title editor.
        if (text_editor::is_active(app.commit_title_editor)) {
            const bool now_visible =
                dialog_field_focused &&
                text_editor::cursor_visible_now(app.commit_title_editor);
            if (now_visible != app.commit_title_editor_blink_last) {
                app.commit_title_editor_blink_last = now_visible;
                invalidate_modal_dialog_area();
            }
        }
        // And for the measure propagate's paste-offset editor.
        if (text_editor::is_active(app.measure_offset_editor)) {
            const bool now_visible =
                dialog_field_focused &&
                text_editor::cursor_visible_now(app.measure_offset_editor);
            if (now_visible != app.measure_offset_editor_blink_last) {
                app.measure_offset_editor_blink_last = now_visible;
                invalidate_modal_dialog_area();
            }
        }
        // (THE DEFERRED CHECKPOINT NOTICE'S POLL stood here from 2026-08-07 until
        // 2026-08-09, when the architect replaced the acknowledge modal with the
        // bottom row's PERMANENT CRITICAL SLOT. A paint-only cell needs no poll
        // and no free strip to wait for: the completion writes the string and
        // damages the row, and the next paint shows it — so the pump is gone with
        // the modal it pumped.)

        if (app.loading || audio.total_frames() <= 0) return;

        // THE RENDER PLAYER'S TICK (2026-08-28), forked at the head: while
        // the mode stands the engine plays the player's item, not the
        // project's audio — the scanner never runs, the audition sequence
        // is cleared at the open and cannot arm — so the player's own tick
        // takes the natural end (through the one stop body, the fence
        // first) and the per-position damage of its clock and scrub, and
        // nothing below this line applies.
        if (app.render_player.active) {
            render_player.tick();
            return;
        }

        // THE A/B AUDITION'S REST DEADLINE, sampled here and ABOVE the
        // playing-only guard below — a rest has nothing playing and no scanner
        // by definition, so a call under that guard would never run during one.
        // This is the run loop's own deadline tick, the same timerfd expiry the
        // platform's key-repeat and touch-disambiguation deadlines ride
        // (maybe_fire_repeat / maybe_resolve_touch_window, input_core.cpp)
        // and the same one the chrome button hold-repeat rides above; the act
        // adds no timer of its own. One enum compare when no rest stands. It
        // sits ahead of ma_playing so a play launched here takes this tick's
        // scanner heartbeat rather than waiting for the next.
        ab_audition.fire_if_due();

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
        //
        // THE A/B AUDITION ADVANCES HERE AND NOWHERE ELSE (architect 2026-08-26):
        // the phase is READ before the stop body (which clears it, being the
        // one stop body every interrupt path also takes) and handed to the
        // advance after. The advance does NOT launch: it switches tabs where
        // the phase asks — synchronously, inside this same tick, so the flip
        // is seen at once — and then ARMS THE REST that precedes the next play
        // (kAuditionPairGapMs inside a pair, kAuditionSwitchGapMs across the
        // switch; the pacing is the architect's own hand, app_state.h). The
        // rest's deadline is sampled by ab_audition.fire_if_due earlier in
        // this same tick body, which is where the launch now happens. A play that
        // was not the act's hands over Idle and the advance does nothing; the
        // natural end of the act's LAST play hands over HomeSecond and the
        // advance arms nothing. The stop body's own call is unchanged: the
        // fence-before-flag-clear ordering above is exactly what the next
        // launch relies on too, a rebind-safe, quiesced device under the fresh
        // play().
        const GuiAuditionSequence ended_audition = app.audition_sequence;
        playback_lifecycle.stop_playback_if_playing();
        ab_audition.advance_after_natural_end(ended_audition);
    });

    gui.set_on_pre_paint([&]() {
        // THE ON-SCREEN KEYBOARD'S SESSION OWNER RUNS AHEAD OF EVERY FRAME,
        // and this hook is the one place a write may still happen with a paint
        // already committed to: it runs before the damage list is read and is
        // allowed to add to it (both backends' paint_one_frame say so), while
        // the painter itself may not — which is why the painter stays a pure
        // reader of the two lamps. A pointer release can open an editor and be
        // followed straight by the backend's paint with no tick between them,
        // so without this call the first frame of a NEW edit could show the
        // previous one's Shift arm or symbol page. It is a no-op with the
        // surface down (the laptop forever) and on any frame whose session did
        // not change, and it sits ABOVE the playback guards below because it
        // has nothing to do with playback.
        onscreen_keyboard::reconcile_session(app, gui, viewport);

        if (app.loading || audio.total_frames() <= 0) return;
        // UNDER THE RENDER PLAYER the engine's cursor is the item's, not the
        // project's: the scanner sampling below would move the waveform's
        // scanner off a buffer the waveform does not show. The player's
        // position is read by its painter and damaged by its own tick.
        if (app.render_player.active) return;
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
        // THE OVERVIEW TICK'S OLD COLUMN, read beside old_px and for the same
        // reason: overview_tick_column reads the still-current
        // playhead_scanner_precise — the last painted position — so this
        // names exactly the lane column the last paint drew the tick at.
        const int ov_old =
            overview_tick_column(app, audio, app.playhead_scanner_precise);
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
        // THE OVERVIEW TICK'S NARROW PAIR (the damage rule's overview
        // extension, playhead_pixel_x, app_state.h): the lane's tick advances
        // at the SCANNER CADENCE, so this per-frame site damages its two 1px
        // lane columns — through the same column owner the painter reads
        // (overview_tick_column), so the damaged column IS the painted one —
        // and only when the column actually moved, which at whole-song scale
        // is once per several seconds. THE HEARTBEAT SITE (the tick lambda
        // above) deliberately carries no overview arm: its job is producing
        // A paint, which its scanner column or clock fallback already does,
        // and the tick's movement is this site's alone. Discrete playhead
        // writes need no arm at all since commit B moved the lane into the
        // centered block: Viewport::invalidate_waveform_area's ONE rect (window
        // top through the waveform's bottom) contains the lane by construction,
        // which is what retired that owner's dedicated overview rider.
        {
            const int ov_new =
                overview_tick_column(app, audio, app.playhead_scanner_precise);
            if (ov_old >= 0 && ov_new >= 0 && ov_new != ov_old) {
                const GuiRect ov_lane = top_overview_row_area(app);
                viewport.invalidate_rect(
                    GuiRect{ov_lane.x + ov_old, ov_lane.y, 1, ov_lane.h});
                viewport.invalidate_rect(
                    GuiRect{ov_lane.x + ov_new, ov_lane.y, 1, ov_lane.h});
            }
        }
        // THE FOLLOW CHASE NEVER PAGES UNDER A LIVE AIM (the two refusal terms
        // below). The chase is the product's one AUTONOMOUS viewport mover: it
        // fires from the clock rather than from an event, and every aiming
        // gesture converts a WINDOW COLUMN through the CURRENT viewport at some
        // later moment than the press the user aimed with —
        //   * the pending nav-surface click (ScrollDragState) converts its
        //     remembered press_x at the RELEASE, so a page in between would
        //     place the playhead on whatever frame had slid under that column;
        //   * the grab-pan's first leg folds the whole press->crossing delta,
        //     and follow suppression only begins once that first scroll_viewport
        //     application fires;
        //   * a live marker / trim / region / strip drag converts each motion's
        //     window x the same way, so a page mid-drag would teleport the
        //     dragged subject under a motionless pointer;
        //   * on glass the conversion is deferred by design — a tap delivers its
        //     whole burst at the LIFT with the DOWN point's coordinates, and the
        //     region hold converts the down point at the beat's expiry.
        // PAUSING THE MOVER is the whole fix: with the chase held, the
        // press-time viewport stays valid by construction and every existing
        // conversion is already correct — nothing captures frames at the press
        // and nothing about the click act, the fold or the session override
        // moves. This is a PAUSE, not a suppression: it writes nothing, so the
        // follow producer inventory (follow_overridden_for_session, app_state.h)
        // is unchanged and the chase simply resumes and catches up on the next
        // tick after the gesture ends — including after a lost button or a
        // force-end that ran no act at all. A long motionless HOLD therefore
        // visibly freezes the chase for as long as it is held, which is the
        // intended reading: the user is aiming.
        // The touch term is the platform's (touch_contact_active — any finger
        // down), because nothing GUI-side is armed during the disambiguation
        // window; the contract is at the touch state block, input_core.h.
        if (app.follow_mode && !app.follow_overridden_for_session &&
            !any_pointer_gesture_active(app) && !gui.touch_contact_active())
            follow_scroll_if_needed();
    });

    // THE GEOMETRY, REDELIVERED: a reopened set's window sends no configure
    // for a size that did not change, so the platform fires on_resize with
    // the standing geometry now that every hook is installed (a no-op on
    // Wayland before the first configure, which delivers it itself; on
    // Android this is also where the first session's owed fire lands).
    gui.redeliver_geometry();

    // Paint the initial background before any synchronous load begins so the
    // window isn't briefly blank on fast disks.
    gui.invalidate_region(0, 0, app.width, app.height);
    gui.drain_events();

    gui.run();

    // WHY run() RETURNED, read before anything below dies: an exit (the
    // platform's own bit — Ctrl+Q, the WM close, a connection loss, a load
    // that failed) ends the process; otherwise the Open project picker's
    // seated name is the project to reopen (the loop contract, platform.h).
    GuiProjectOutcome outcome;
    if (!gui.exit_requested()) outcome.reopen = app.reopen_project;

    // THE TEARDOWN, in this order. The platform forgets the five worker fds
    // first — the workers close them below, and the next session registers
    // its own — then the audio device goes down before the sample buffer
    // goes out of scope.
    gui.set_worker_completion_fd(-1, {});
    gui.set_waveform_worker_completion_fd(-1, {});
    gui.set_history_worker_completion_fd(-1, {});
    gui.set_history_prefetch_completion_fd(-1, {});
    gui.set_sync_worker_completion_fd(-1, {});
    // The car's hook goes with them (its install above says why it alone of
    // the handlers is cleared): its producer is another thread that keeps
    // producing between sessions.
    gui.set_on_media_command({});
    // ON ANDROID THIS IS ONE STREAM STOP AND ONE START PER REOPEN: the AAudio
    // stream stays started between plays for the transient's sake, and a
    // reopen is the one moment it is torn down and brought back (the next
    // session's init owns the rate, and projects may differ in it) — accepted,
    // one transient per project change rather than per play.
    playback.shutdown();
    // Join the render worker before cache teardown so a render completing
    // during shutdown cannot touch the dismantled cache. Idempotent; the
    // destructor's later call is then a no-op. A render still in flight is
    // cancelled by the join — killed as any dispatch kills it.
    async_renderer.shutdown();
    // Blocks on an in-flight checkpoint rather than abandoning a git child
    // mid-act; the piece is already saved (the act saves before it dispatches),
    // so the wait costs a moment and never any work.
    history_commit_worker.shutdown();
    // The prefetch abandons its scan at the next candidate boundary rather than
    // being waited out: it writes nothing anywhere.
    history_prefetch.shutdown();
    // Blocks on an in-flight synchronization rather than leaving a truncated
    // wav on the volume; the copies are already the user's intent, so the wait
    // costs a moment and never any work.
    external_sync_worker.shutdown();
    // Everything else — the caches, the handlers, the callbacks' captured
    // objects — dies with this frame, in reverse construction order; the
    // window keeps the callbacks' std::function shells until the next session
    // overwrites them, and calls none in between.
    return outcome;
}

} // namespace

int gui_main(const char* argument) {
    if (!verify_c_numeric_locale("warptempo_gui")) return 1;

    // Auto-reap the fire-and-forget children the GUI launches. Ignoring SIGCHLD
    // makes the kernel discard child exit status so a detached child never
    // lingers as a zombie; set once here, never per-launch.
    //
    // THE WHOLE ROSTER OF FORKING SUBSYSTEMS, and every one of them is written
    // TO this disposition rather than against it (the external audio player
    // `l` used to spawn left the roster 2026-08-28 — `l` now plays a render
    // in-process through the render player, render_player.h):
    //   * THE HISTORY MODE (history_diff.cpp), which runs git SYNCHRONOUSLY
    //     through two fenced entry points — reads for the diff and the walk,
    //     and the commit act's `add`/`commit`/`push` — and, because this
    //     disposition makes waitpid return ECHILD, decides nothing from child
    //     status: a read reports whether it RAN (an exec self-pipe) and what it
    //     said, and the commit act's verdicts are observations of the
    //     repository afterwards.
    //   * THE TRASHED DELETION's `gio trash` (trash_directory,
    //     input_key_dispatch.cpp), whose verdict is likewise an observation of
    //     the filesystem rather than an exit code.
    // Each helper's own comment owns its reasoning; what matters here is that
    // none of them depends on the default disposition being restored.
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
    // no SIGPIPE-dependent behaviour of its own. THE CHILDREN INHERIT IT: an
    // ignored disposition survives exec, and the roster's two spawners (git
    // through history_diff.cpp, gio through trash_directory) run the child
    // with the parent reading its output to the end or discarding it, so the
    // child never meets a closed pipe. (The one spawner that reset SIGPIPE
    // and SIGCHLD to default for its child — the external audio player's —
    // retired 2026-08-28 with the `l` spawn.)
    std::signal(SIGPIPE, SIG_IGN);

    // (NO PALETTE LOAD HERE ANY MORE. The colors were 23 mutable globals filled
    // from ~/.config/warptempo_gui/colors.conf by load_color_config() at exactly
    // this point — before the first paint and before anything could derive a
    // value from them. The whole system retired 2026-08-02: the palette is
    // constexpr, so there is nothing to initialize and every reader, the
    // waveform worker thread included, sees compile-time constants. The record
    // is at the palette block, render.h.)

    // THE DEVICE CONFIG, READ BEFORE THERE IS A WINDOW (architect 2026-08-27).
    // Its five keys describe the MACHINE, not the piece, so they live in
    // `$XDG_CONFIG_HOME/warptempo_gui/config` rather than in a source's
    // `.settings` (the file, its schema and its strictness are
    // device_config.h's). A first run on either device stamps the BACKEND's
    // own template — the seam's device_config_defaults(), the one platform fact
    // the GUI proper needs here — and then reads it back like any other launch.
    //
    // IT IS FATAL AND IT IS EARLY. A malformed config is the adversarial class
    // (the file is program-written, so a violation is a hand edit): one blunt
    // terminal line naming the path and the offending line, and no window at
    // all — no repair, no partial apply, and above all no silent fallback to
    // defaults, which would quietly discard a value the user typed.
    //
    // THIS STRUCT IS THE LIVE CONFIG FOR THE PROCESS'S WHOLE LIFE: it outlives
    // every project the loop below opens, each session's AppState reaches it by
    // pointer, and every commit that writes the file writes through it — so a
    // value typed in one project survives the reopen that tears that AppState
    // down, and a failed persist (advisory) loses nothing the user committed.
    // Re-reading the file per session was the alternative and is refused for
    // exactly that loss (the rule is at write_device_config, device_config.h).
    DeviceConfig device_config;
    {
        auto cfg = load_device_config(GuiPlatform::device_config_defaults());
        if (!cfg) {
            std::fprintf(stderr, "warptempo_gui: %s\n", cfg.error().c_str());
            return 1;
        }
        device_config = *cfg;
    }
    // THIS IS THE SCALE'S INIT ROAD ON BOTH BACKENDS, and it runs BEFORE
    // gui.init(): the two pushes here install the scale into the renderer and
    // the input core, so the FIRST configure and the FIRST painted frame are
    // already at the user's scale. That is strictly earlier than the sidecar
    // road it replaced — which could not run until a source had been parsed on
    // the startup tick, so the tablet's first frame used to paint at 100 % and
    // jump. The other road is the settings editor's `gui_scale=` commit
    // (GuiInputHandler::apply_gui_scale); the touch-slop inventory is at
    // GuiInputCore::set_touch_slop_px.
    set_gui_scale_percent(device_config.gui_scale);

    // WHICH PROJECT OPENS FIRST — the project model's two roads (startup_source,
    // project_model.h): the argument, which must be a project's source under
    // the config's projects path, or with none the remembered project and then
    // the first valid one in name order. None is the blunt terminal line and
    // exit 1: the app always has a project open, and there is no empty state
    // to open a picker over.
    auto first = startup_source(device_config, argument);
    if (!first) {
        std::fprintf(stderr, "warptempo_gui: %s\n", first.error().c_str());
        return 1;
    }

    // THE PLATFORM, ONCE PER PROCESS (the loop contract, platform.h). The
    // touch slop is a SCALED LENGTH and the core sits below the GUI model, so
    // the GUI resolves it and hands it down; it follows the push above because
    // drag_moved_threshold_px() reads what that call installed. The window
    // itself opens inside the first session (run_project), which owns the cold
    // size it is opened at.
    GuiPlatform gui;
    gui.set_touch_slop_px(drag_moved_threshold_px());

    // THE RENDER CACHE, ONCE PER PROCESS: its keying is fingerprint-only — the
    // source path and its identity are INSIDE the key (render_fingerprint,
    // render_cache.h) — so nothing in it is per source, and one directory per
    // process is what init() creates and shutdown() removes. Shared by every
    // session's target_render, which holds it by reference. A failed init()
    // leaves the cache disabled (every lookup misses), so target_render needs
    // no special-casing.
    RenderCache render_cache;
    render_cache.init();

    // THE PROJECT LOOP. Everything ONE PER PROCESS is above; everything ONE PER
    // PROJECT is run_project's, built around the session's source and torn
    // down before it returns. run() returns for two reasons — an exit, and
    // the Open project picker's REOPEN — and the outcome carries which. WHAT IS
    // PER-PROCESS BESIDES THESE, by inventory: the two signal dispositions,
    // the renderer's file-scope scale (set_gui_scale_percent), the text
    // shaper's face caches and the bundled-font state (gui_font_bundled.cpp),
    // the bottom row's clock metrics memo (keyed on the text size, not the
    // piece), the modal session-id counter (text_editor::next_session_id —
    // monotonic for the process, so no id repeats across reopens), the strict
    // load's scratch serial (history_diff.cpp), and on Android the glue
    // pointer g_android_app and the stdio routing. Every other static in the
    // GUI is either a constant or refreshed on every call
    // (active_display_context).
    //
    // A LOAD THAT FAILS NEVER REACHES A REOPEN: the prompt checked validity
    // and ran the load's own failure arms at Enter, before this loop tore the
    // previous set down, so the folder resolved below can refuse only by a
    // change on disk in between — the adversarial class, and it takes the
    // same fatal exit a failed load has always taken.
    GuiProjectSource project = *first;
    bool window_up   = false;
    int  exit_status = 0;
    for (;;) {
        const GuiProjectOutcome outcome =
            run_project(gui, device_config, render_cache, project, window_up);
        if (outcome.reopen.empty()) {
            exit_status = outcome.exit_status;
            break;
        }
        auto next = resolve_project(
            std::filesystem::path(device_config.projects_path) /
            outcome.reopen);
        if (!next) {
            std::fprintf(stderr, "warptempo_gui: %s\n", next.error().c_str());
            exit_status = 1;
            break;
        }
        project = *next;
    }

    gui.shutdown();
    // Remove this process's render-cache directory.
    render_cache.shutdown();
    return exit_status;
}
