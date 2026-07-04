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
#include "waveform_worker.h"
#include "warpmarkers.h"
#include "file_loader.h"
#include "flag_editor.h"
#include "input_handler.h"
#include "paint_handler.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "render.h"
#include "render_pipeline.h"
#include "render_view.h"
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <csignal>
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

// kFlagFontSize, kTimestampPadX, and kTabLetterGapPx now live in
// paint_handler.h so paint_handler.cpp can reach them; the constants below
// are paint-handler-independent and stay file-local. (kProgressBarHeight,
// formerly in this group, was deleted with the load progress bar.)

// The window-proportional strip ratios were replaced with a fixed-pixel
// mirrored grid; the strip/row geometry now derives from monospace_row_h(),
// kRowGapPx, and kFlagBottomLiftPx (see the geometry helpers below).

// kMarkerHitHalfPx moved to app_state.h so the hit_test_* free
// functions and the GuiInputHandler mouse handler can reach them.

// ms-per-pixel for each numeric zoom level. Level 1 is most zoomed in;
// level 0 is the fit-file sentinel (table bypassed in samples_per_pixel_at).
// Index N holds the ms/px for level N; each numeric step is exactly 2x the
// previous. kZoomTableSize (in app_state.h) is the size of this table.
constexpr double kZoomMsPerPixel[] = {
    -1.0,    // [0]  fit-file sentinel
     0.625,  // [1]  1.2 s  — deepest, manual zoom-in only
     1.25,   // [2]  2.4 s  — snap level (kSnapZoomLevel)
     2.5,    // [3]  4.8 s
     5.0,    // [4]  9.6 s
    10.0,    // [5]  19.2 s
    20.0,    // [6]  38.4 s
    40.0,    // [7]  76.8 s
    80.0,    // [8]  153.6 s
   160.0,    // [9]  307.2 s
   320.0,    // [10] 614.4 s
};
static_assert(sizeof(kZoomMsPerPixel) / sizeof(kZoomMsPerPixel[0])
              == static_cast<size_t>(kZoomTableSize),
              "kZoomMsPerPixel size must match kZoomTableSize");

// kPlayheadHalfPx (half-width of the column invalidated around a playhead
// position) now lives in render.h as a single shared inline constexpr,
// reached here via the render.h include.

// The BPM-sweep math primitive (BaseTempoScale + compute_base_tempo_scale)
// moved out of this anonymous namespace into input_handler.h so
// input_handler.cpp can reach it. GuiInputHandler::render_bpm_sweep is the
// sole caller.

// compute_hover_popup_text lives in the parser (frame_map_build.{cpp,h})
// and operates on the parser's WarpMarker. It is a different translation
// unit from flag_text_for_marker, which stays in render.cpp over
// GuiWarpMarker.

// UndoEntry, DragState, UndoHistory, PlayheadDragState,
// HoverPopupState, DialogTrigger, PromptState, ViewState, AppState live in
// app_state.h, alongside the Viewport struct.


// ParsedSettings + the settings parse / format / write helpers
// moved to settings_io.{h,cpp} so file_loader.cpp and save_markers can
// both reach them.

// WaveformCache was promoted to paint_handler.{h,cpp} so paint_handler.cpp can
// reach it. The instance is still a local in main() and is passed by reference
// into GuiPaintHandler.

} // namespace

// Geometry helpers — public to viewport.cpp via app_state.h. samples_per_pixel_at
// remains main-private (`static`).
//
// Fixed-pixel mirrored four-row grid. Top and bottom strips are equal
// pixel height regardless of window size; the waveform flexes in the middle.
// Each strip is two text rows of the cached row height monospace_row_h(),
// packed tight: the inter-row gap kRowGapPx (G), the waveform-side gap, and the
// outer (window-edge) gap (both kFlagBottomLiftPx) are all 0, so strip_h is just
// 2*row_h, the two rows touch, and the strips sit flush against the window edges
// and the waveform area. The four row rects are the same formula with the y
// flipped about the window midline: a bottom row's y is `h - <top row's y> -
// row_h`.

// Defensive backstop only: floor the window dims to the 640x480 minimum before
// any geometry arithmetic so no code path can compute a negative/zero waveform,
// regardless of what the compositor sends. Mirrors the set_min_size hint.
static void clamp_dims(int& w, int& h) {
    if (w < kMinWindowWidthPx)  w = kMinWindowWidthPx;
    if (h < kMinWindowHeightPx) h = kMinWindowHeightPx;
}

// outer_gap(0) + row_h + G(0) + row_h + waveform_gap(0) = 2*row_h. All three
// gaps are now zero; the form below keeps the derivation explicit so the gap
// terms reappear if any constant is un-zeroed. Dimension-independent.
int strip_h(const AppState&) {
    return 2 * monospace_row_h()
         + static_cast<int>(kRowGapPx)
         + 2 * static_cast<int>(kFlagBottomLiftPx);
}

GuiRect top_strip_area(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    return GuiRect{0, 0, w, strip_h(a)};
}

GuiRect bottom_strip_area(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    const int sh = strip_h(a);
    return GuiRect{0, h - sh, w, sh};
}

GuiRect waveform_area(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    const int sh = strip_h(a);
    return GuiRect{0, sh, w, h - 2 * sh};
}

// Top strip rows, counted down from the window top with all gaps now zero:
// top_upper_row starts flush at y=0 (the b/e trim-flag row), top_lower_row
// sits immediately below it (the regular warp/phase-reset flag chips), and the
// waveform area begins immediately below that. The regular chip's bottom edge
// is therefore flush with the waveform area top — exactly flag_chip_bottom_y.
GuiRect top_upper_row_area(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    const int row_h = monospace_row_h();
    const int y = static_cast<int>(kFlagBottomLiftPx);
    return GuiRect{0, y, w, row_h};
}

GuiRect top_lower_row_area(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    const int row_h = monospace_row_h();
    const int y = static_cast<int>(kFlagBottomLiftPx)
                + row_h + static_cast<int>(kRowGapPx);
    return GuiRect{0, y, w, row_h};
}

// Bottom strip rows, each the mirror of a top row about the window midline
// (bottom_y = h - top_y - row_h). bottom_upper_row (inner, nearest waveform) is
// the mirror of top_lower_row and carries the modal/editor/queue/hover chain;
// bottom_lower_row (outer, nearest window edge) is the mirror of top_upper_row
// and carries the always-on status line.
GuiRect bottom_upper_row_area(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    const int row_h = monospace_row_h();
    const int top_y = static_cast<int>(kFlagBottomLiftPx)
                    + row_h + static_cast<int>(kRowGapPx);
    return GuiRect{0, h - top_y - row_h, w, row_h};
}

GuiRect bottom_lower_row_area(const AppState& a) {
    int w = a.width, h = a.height;
    clamp_dims(w, h);
    const int row_h = monospace_row_h();
    const int top_y = static_cast<int>(kFlagBottomLiftPx);
    return GuiRect{0, h - top_y - row_h, w, row_h};
}

// Resolve the trim region from AppState's settings-side trim fields.
// Absent has_trim_* falls back to [0, total_frames]. Banker's rounding
// converts seconds to samples. Clamps to [0, total_frames] and never
// returns end < begin.
std::pair<long long, long long> compute_trim_samples(
    const AppState& a, int sample_rate, long long total_frames) {
    long long begin = 0;
    long long end   = total_frames;
    const double sr = static_cast<double>(sample_rate);

    if (a.trim.has_begin) {
        begin = static_cast<long long>(
            std::nearbyint(a.trim.begin_seconds * sr));
    }
    if (a.trim.has_end) {
        end = static_cast<long long>(
            std::nearbyint(a.trim.end_seconds * sr));
    }
    if (begin < 0) begin = 0;
    if (begin > total_frames) begin = total_frames;
    if (end > total_frames) end = total_frames;
    if (end < begin) end = begin;
    return {begin, end};
}

static double samples_per_pixel_at(int zoom_level,
                                   int waveform_width_px,
                                   int64_t total_frames,
                                   int sample_rate) {
    if (zoom_level == kFitFileLevel) {
        if (waveform_width_px <= 0) return 1.0;
        double spp = static_cast<double>(total_frames) /
                     static_cast<double>(waveform_width_px);
        if (spp < 1e-9) spp = 1e-9;
        return spp;
    }
    // Documents the contract: the fit-file branch above must catch level 0;
    // the -1.0 sentinel at kZoomMsPerPixel[0] would surface as garbage spp
    // otherwise. Numeric levels are kMinNumericLevel..kMaxNumericLevel.
    assert(zoom_level >= kMinNumericLevel &&
           zoom_level <= kMaxNumericLevel);
    return kZoomMsPerPixel[zoom_level] *
           static_cast<double>(sample_rate) / 1000.0;
}

// Largest numeric level L (in [kMinNumericLevel, kMaxNumericLevel]) whose
// samples_visible does not exceed total_frames. Returns -1 if even
// kMinNumericLevel shows more than the file — in which case fit-file is the
// only valid level.
int max_valid_numeric_level(int waveform_width_px,
                            int64_t total_frames,
                            int sample_rate) {
    int best = -1;
    for (int L = kMinNumericLevel; L <= kMaxNumericLevel; L++) {
        const double spp =
            samples_per_pixel_at(L, waveform_width_px, total_frames, sample_rate);
        const double visible = spp * waveform_width_px;
        if (visible <= static_cast<double>(total_frames)) best = L;
        else break; // table is monotonic
    }
    return best;
}

int64_t live_total_frames(const AppState& a, const GuiAudio& audio) {
    int64_t result = audio.total_frames();
    if (a.active_audio_view == 'T' && !a.render_view.enabled) {
        const TargetMapCache& c = target_view_map_cached(
            a, audio.sample_rate(),
            static_cast<long>(audio.total_frames()));
        if (c.tgt_total_frames > 0) result = c.tgt_total_frames;
    }
    return result;
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

void clamp_viewport_start(AppState& a, const GuiAudio& audio) {
    const int64_t visible = samples_visible(a, audio);
    const int64_t total   = live_total_frames(a, audio);
    if (visible >= total) {
        a.viewport_start_sample = 0;
        return;
    }
    if (a.viewport_start_sample < 0) a.viewport_start_sample = 0;
    const int64_t max_start = total - visible;
    if (a.viewport_start_sample > max_start) a.viewport_start_sample = max_start;
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
    return static_cast<double>(a.playhead_scanner_sample - vp_start) / spp;
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
    const int col = static_cast<int>(std::floor(px_x + 0.5));
    const int x0 = std::max(area.x, col - kPlayheadHalfPx);
    const int x1 = std::min(area.x + area.w, col + kPlayheadHalfPx + 1);
    if (x1 <= x0) return GuiRect{area.x, 0, 0, 0};
    // Envelope extends up from the top of the window to the bottom of the
    // waveform area so it covers the playhead line inside the waveform AND
    // the triangle indicator in the flag strip above it.
    const int y0 = 0;
    const int y1 = area.y + area.h;
    return GuiRect{x0, y0, x1 - x0, y1 - y0};
}

// The status line and the transient/modal chain now occupy the two
// rows of the bottom strip, so the invalidation region is the whole bottom
// strip rect (both rows repaint together).
GuiRect timestamp_invalidate_rect(const AppState& a) {
    return bottom_strip_area(a);
}


int main(int argc, char** argv) {
    if (!verify_c_numeric_locale("warptempo_gui")) return 1;

    const char* cli_path = nullptr;
    if (argc == 1) {
        // Empty window; wait for a drag-and-drop.
    } else if (argc == 2) {
        cli_path = argv[1];
    } else {
        std::fprintf(stderr, "usage: warptempo_gui [<audio_file>]\n");
        return 1;
    }

    // Wayland clipboard source writes go to consumer-owned pipes.
    // Ignoring SIGPIPE turns a vanishing consumer into EPIPE for the send loop.
    std::signal(SIGPIPE, SIG_IGN);

    AppState     app;
    GuiAudio     audio;
    GuiPlayback  playback;
    GuiPlatform  gui;
    WaveformCache wf_cache;
    // Marker stems live on their own surface, rebuilt synchronously from
    // on_tick. Constructed alongside wf_cache so they share the same lifetime;
    // passed by reference into GuiPaintHandler and (for the destroy_surface
    // hook) GuiFileLoader.
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
    // The viewport-mutator and invalidation lambdas have been hoisted
    // onto the Viewport struct in viewport.{cpp,h}. The lambdas below are
    // one-line forwarders so callsites elsewhere in main() don't need to
    // change. The promoted timestamp invalidation helper now lives on
    // Viewport, so all of its former callsites now call
    // invalidate_timestamp_area directly.
    //
    // The nine std::function forward-declares previously kept here
    // (recompute_hover_at_cursor, clear_hover_popup, stop_playback_if_playing,
    // refresh_active_tab_view_from_app, save_markers, request_close_or_revert,
    // prompt_activate_response, toggle_playback, set_playback_speed) were
    // retired. The two substantive bodies moved onto Viewport
    // (clear_hover_popup, recompute_hover_at_cursor); the seven forwarders
    // are now direct method calls on their owning struct
    // (viewport.clear_hover_popup, playback_lifecycle.stop_playback_if_playing
    // / toggle_playback / set_playback_speed, save_ops.save,
    // prompt.request_close_or_revert / activate_response,
    // active_views.refresh_active_tab_view_from_app).

    Viewport viewport(app, audio, gui, playback);
    GuiPlaybackLifecycle playback_lifecycle(app, audio, gui, playback, viewport);
    Selection selection(app, audio, viewport, playback);
    GuiAsyncRenderer async_renderer;
    if (!async_renderer.init()) {
        std::fprintf(stderr,
            "warptempo_gui: failed to start async renderer; exiting\n");
        return 1;
    }
    // Waveform-cache rebuild runs on this dedicated worker; the
    // paint thread becomes blit-only. Must be constructed before
    // GuiPaintHandler (which takes it as a reference) and before
    // GuiFileLoader (which calls wait_until_idle before swapping audio).
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
    // file_loader's clear sites call target_render.cancel_for_load(),
    // so it must be constructed after target_render.
    GuiFileLoader file_loader(app, audio, gui, playback, wf_cache, stem_cache,
                              flag_cache, waveform_worker, viewport,
                              target_render);
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
    GuiRenderView render_view(app, audio, playback, gui, selection,
                              viewport, active_views, target_render);
    GuiPaintHandler paint_handler(app, audio, playback, wf_cache, stem_cache,
                                  flag_cache, waveform_worker, gui);
    PhaseResetPropagate phase_reset_propagate(app, viewport, undo,
                                              target_render, active_views);
    GuiSaveOps save_ops(app, undo, active_views, viewport);
    GuiPrompt prompt(app, gui, viewport, file_loader,
                     phase_reset_propagate, save_ops);
    GuiSettingsEditor settings_editor(app, audio, viewport, active_views, undo,
                                      target_render);
    gui.set_worker_completion_fd(async_renderer.completion_fd(),
        [&async_renderer]() { async_renderer.on_completion_event(); });
    gui.set_waveform_worker_completion_fd(waveform_worker.completion_fd(),
        [&waveform_worker]() { waveform_worker.on_completion_event(); });
    GuiInputHandler input_handler(app, audio, gui, playback,
                                  viewport, selection, undo,
                                  warpops, phase_resets, marker_drag,
                                  flag_editor,
                                  render_view, active_views,
                                  phase_reset_propagate,
                                  async_renderer,
                                  playback_lifecycle, save_ops, prompt,
                                  settings_editor, target_render,
                                  paint_handler);

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
        [&](int64_t new_vp) { paint_handler.pan_waveform_incremental(new_vp); };

    // One-shot discrete jumps route here instead of the async worker: render
    // the plate synchronously and publish the displayed fingerprint now so the
    // overlays and waveform land in the same frame. See
    // Viewport::kick_waveform_sync and
    // GuiPaintHandler::force_synchronous_waveform_rebuild.
    viewport.request_waveform_sync_ =
        [&]() { paint_handler.force_synchronous_waveform_rebuild(); };

    auto invalidate_timestamp_area   = [&]() { viewport.invalidate_timestamp_area(); };
    auto invalidate_playhead_columns = [&](double a, double b) { viewport.invalidate_playhead_columns(a, b); };
    auto follow_scroll_if_needed     = [&]() { viewport.follow_scroll_if_needed(); };

    std::string pending_initial_load = cli_path ? std::string(cli_path) : std::string();
    bool        initial_load_done    = false;

    // -- Redraw -------------------------------------------------------------

    gui.set_on_redraw([&](cairo_t* cr, int x, int y, int w, int h) {
        paint_handler.on_redraw(cr, x, y, w, h);
        if (app.playhead_scanner_restore_pending) {
            app.playhead_scanner_endpoint_painted = true;
        }
    });

    gui.set_on_resize([&](int w, int h) {
        paint_handler.on_resize(w, h);
    });

    auto invalidate_top_strip     = [&]() { viewport.invalidate_top_strip(); };

    // popup_eligible_marker moved to a free function in
    // app_state.{h,cpp}. The remaining callers in this TU
    // (Viewport::recompute_hover_at_cursor, on_tick) reach it directly
    // with the new (app, idx) signature; on_motion calls it from
    // input_handler.cpp. The hover-popup and iteration-mode comments live
    // above the declaration in app_state.h.

    // The drag and selection-shift lambdas have been hoisted onto
    // the GuiWarpMarkersOps struct in warpmarkers_ops.{cpp,h}.

    // The shared wheel handler (handle_wheel) moved to
    // GuiInputHandler as a private helper method. on_button_press is
    // its only caller.

    // The multi-render queue runner (run_render_batch +
    // RenderBatchResult) had no callers outside the on_key body. It moved
    // to GuiInputHandler as a private helper method (see input_handler.h).

    gui.set_on_key([&](GuiKey key, GuiInputState mods) {
        input_handler.on_key(key, mods);
    });

    gui.set_on_close([&]() {
        // Window-manager close (title-bar X) routes through the unsaved-
        // work dialog when dirty, same as Ctrl+Q.
        prompt.request_close_or_revert(DialogTrigger::CLOSE_WINDOW);
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

    gui.set_on_motion([&](int mouse_x, int mouse_y, GuiInputState mods) {
        input_handler.on_motion(mouse_x, mouse_y, mods);
    });

    // -- File loading --------------------------------------------------------
    //
    // load_file, revert_to_blank, and load_then_drain moved to
    // file_loader.{h,cpp} on GuiFileLoader. The drop-accept predicate and
    // the on_file_drop handler stay here as one-line lambdas that capture
    // file_loader; the predicate has no reference to the loader.

    gui.set_drop_accept_predicate([&](int x, int y) -> bool {
        const GuiRect area = waveform_area(app);
        return x >= area.x && x < area.x + area.w &&
               y >= area.y && y < area.y + area.h;
    });

    gui.set_on_file_drop([&](const std::string& path) {
        if (app.loading) {
            app.pending_drop_path = path;
            return;
        }
        file_loader.load_then_drain(path);
    });

    // Tick: runs once per event-loop iteration. During playback, snapshots
    // the audio thread's cursor and mirrors it into the main-thread playhead,
    // invalidating just the columns and timestamp that changed. Also
    // detects natural end-of-playback via the atomic playing flag.
    gui.set_on_tick([&]() {
        // Startup file load, deferred out of pre-run() so the window maps and
        // paints first (the compositor's initial configure / first frame only
        // land once run() is pumping). Gated on has_initial_configure() so the
        // load — and its loading notice — run against a mapped, painted surface.
        // Mirrors on_file_drop, which already loads from inside the loop. Drops
        // queued during the startup load run immediately after, as the old
        // pre-run block did.
        if (!initial_load_done && !pending_initial_load.empty() &&
            gui.has_initial_configure()) {
            initial_load_done = true;
            const std::string p = std::move(pending_initial_load);
            pending_initial_load.clear();
            file_loader.load_file(p);
            while (!app.pending_drop_path.empty()) {
                std::string next = std::move(app.pending_drop_path);
                app.pending_drop_path.clear();
                file_loader.load_file(next);
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
        // (scale commit, tempo edit, marker move while target view is
        // displayed), the current zoom level and viewport may sit outside
        // the new bounds. Re-clamp both; when either actually moved, the
        // displayed geometry changed discretely, so rebuild synchronously
        // (same class as drop_marker — see warpmarkers_ops.cpp).
        {
            const int64_t lt = live_total_frames(app, audio);
            if (app.last_tick_live_total != lt) {
                app.last_tick_live_total = lt;
                bool changed = false;
                const GuiRect area = waveform_area(app);
                const int max_l = max_valid_numeric_level(
                    area.w, lt, audio.sample_rate());
                if (app.zoom_level >= kMinNumericLevel) {
                    const int clamped =
                        (max_l < kMinNumericLevel) ? 0   // fit-file only
                                                   : std::min(app.zoom_level, max_l);
                    if (clamped != app.zoom_level) {
                        app.zoom_level = clamped;
                        changed = true;
                    }
                }
                const int64_t old_vp = app.viewport_start_sample;
                clamp_viewport_start(app, audio);
                if (app.viewport_start_sample != old_vp) changed = true;
                if (changed) {
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

        if (app.loading || audio.total_frames() <= 0) return;

        const bool ma_playing = playback.is_playing();
        if (!app.playhead_scanner_active && !ma_playing) return;

        if (ma_playing) {
            // Heartbeat: invalidate the scanner column at the current
            // model position so the paint cycle keeps running. The
            // pre-paint hook reads the predictor at paint time and adds
            // damage for the actually-painted position. We do not read
            // the predictor or update app.playhead_scanner_sample here
            // — that work moved to the pre-paint hook to eliminate the
            // tick/paint sampling-rate mismatch that caused playhead
            // motion to stutter at high zoom. The timestamp area is
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
        // scanner on the exclusive end bound for one paint, then restore it to
        // the launch position on the following tick.
        if (app.playhead_scanner_active) {
            const int64_t endpoint =
                (app.active_audio_view == 'T' &&
                 !app.render_view.enabled &&
                 app.target_buffer_frames > 0)
                    ? (app.target_buffer_start_frame +
                       app.target_buffer_frames)
                    : viewport.trim_end_sample();
            playback_lifecycle.hold_natural_end_scanner(endpoint);
            if (app.follow_mode && !app.follow_overridden_for_session)
                follow_scroll_if_needed();
        }
    });

    gui.set_on_pre_paint([&]() {
        if (app.loading || audio.total_frames() <= 0) return;
        if (!playback.is_playing()) return;

        // Read the predictor at paint time. The predictor is continuous
        // in wall time, so this gives the freshest possible position
        // right before paint consumes the damage list. Under the
        // split-playhead model the predictor advances the scanner only
        // — the cursor stays where the user left it.
        const int64_t cur = playback.cursor();
        // Target view: playback.cursor() is a target-buffer-frame
        // index in [0, target_buffer_frames). app.playhead_scanner_sample
        // is full-target-frame. Translate at the boundary. Source view
        // and render view: identity (impl_->samples points at the
        // source's own audio for both; cursor is already in the
        // active-domain coordinate system). The target_buffer_frames
        // > 0 guard ensures the bias is only applied when a successful
        // target render has populated the buffer; pre-paint can fire
        // briefly between dispatch and on_render_done if a paint races
        // a cancel, and applying a stale bias against the source-bound
        // buffer would skew the playhead.
        int64_t translated = cur;
        if (app.active_audio_view == 'T' &&
            !app.render_view.enabled &&
            app.target_buffer_frames > 0) {
            translated = cur + app.target_buffer_start_frame;
        }
        if (translated == app.playhead_scanner_sample) return;

        // One-shot read of the viewport-mutation stash. When set, it
        // holds the scanner's last painted pixel-x under the OLD
        // viewport; the recomputed scanner_pixel_x against the new
        // viewport would point at a column the scanner was never
        // painted at, leaving a ghost.
        double old_px;
        if (app.playhead_scanner_old_px_stash >= 0.0) {
            old_px = app.playhead_scanner_old_px_stash;
            app.playhead_scanner_old_px_stash = -1.0;
        } else {
            old_px = scanner_pixel_x(app, audio);
        }
        app.playhead_scanner_sample = translated;
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
    // Remove this process's render-cache directory and free the RAM tier.
    render_cache.shutdown();
    return 0;
}
