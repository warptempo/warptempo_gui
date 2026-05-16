#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
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
#include "save_ops.h"
#include "selection.h"
#include "settings_editor.h"
#include "settings_io.h"
#include "tab_mode.h"
#include "target_render.h"
#include "text_display.h"
#include "text_editor.h"
#include "time_format.h"
#include "phase_reset_markers.h"
#include "phase_reset_markers_ops.h"
#include "prompt.h"
#include "undo.h"
#include "viewport.h"
#include "warpmarkers_ops.h"
#include "platform_wayland.h"

#include <cairo/cairo.h>
#include <sndfile.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
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

// X.7.8a: kProgressBarHeight, kChannelGapPx, kFlagFontSize, kTimestampPadX,
// kTimestampBaselineFromBottom, kTabLetterGapPx, kIterPopupVerticalGapPx,
// and kIterPopupVPadExtraPx now live in paint_handler.h so paint_handler.cpp
// can reach them; the constants below are paint-handler-independent and
// stay file-local.

constexpr double   kTopStripRatio     = 0.10;
constexpr double   kBottomStripRatio  = 0.10;

// X.7.8b-2: kMarkerHitHalfPx, kDoubleClickMs, kDoubleClickPixels moved to
// app_state.h so the hit_test_* free functions and the GuiInputHandler
// mouse handler can reach them.

// Time the cursor must dwell on a popup-eligible flag rect before the
// hover popup appears. Distinct from kDoubleClickMs (point-event window
// vs continuous-state duration) even though they currently share a value.
constexpr int kHoverDelayMs       = 500;

// ms-per-pixel for each numeric zoom level. Level 1 is most zoomed in;
// level 0 is the fit-file sentinel (table bypassed in samples_per_pixel_at).
// Index N holds the ms/px for level N; each numeric step is exactly 2x the
// previous. kZoomTableSize (in app_state.h) is the size of this table.
constexpr double kZoomMsPerPixel[] = {
    -1.0,    // [0]  unused — key 0 = level 0 = fit-file, table bypassed
     1.25,   // [1]  level 1: most zoomed in (2.4 s visible at 1920 px)
     2.5,    // [2]  level 2: 4.8 s
     5.0,    // [3]  level 3: 9.6 s
    10.0,    // [4]  level 4: 19.2 s
    20.0,    // [5]  level 5: 38.4 s
    40.0,    // [6]  level 6: 76.8 s
    80.0,    // [7]  level 7: 153.6 s
   160.0,    // [8]  level 8: 307.2 s
   320.0,    // [9]  level 9: 614.4 s (most zoomed out numeric)
};
static_assert(sizeof(kZoomMsPerPixel) / sizeof(kZoomMsPerPixel[0])
              == static_cast<size_t>(kZoomTableSize),
              "kZoomMsPerPixel size must match kZoomTableSize");

// Region width includes room for the A/B tab letter and the dirty indicator
// past the timestamp text edge.
constexpr int kTimestampRegionW           = 200;
constexpr int kTimestampRegionH           = 30;
constexpr double kDirtyGapPx              = 8.0;

// Half-width of the column invalidated around a playhead position. Wide
// enough to cover the playhead line, the 17px-wide triangle indicator
// (±8 px of playhead_x including the tip column), and subpixel
// rounding margin.
constexpr int kPlayheadHalfPx = 8;

// X.7.8b-1: Brief X.3 BPM-sweep math primitive (BaseTempoScale +
// compute_base_tempo_scale) moved out of this anonymous namespace into
// input_handler.h so input_handler.cpp can reach it. on_key (Ctrl+Alt+M)
// is the sole caller after this brief.

// X.7.8a: IterPopupHit, BpmPopupHit, and the compute_*_popup_hits
// helpers were promoted to paint_handler.{h,cpp} so paint_handler.cpp
// can reach them. They remain reachable from this TU via
// `#include "paint_handler.h"` below.

// X.7.8b-3: compute_hover_popup_text moved to render.{h,cpp} so
// input_handler.cpp can reach it from on_motion. It sits next to
// resolve_inherited_tempo / flag_text_for_marker — same rendering-
// time text formatting role over GuiWarpMarker, same TU.

// OpKind, UndoEntry, DragState, UndoHistory, PlayheadDragState,
// HoverPopupState, DialogTrigger, PromptState, ViewState, AppState live in
// app_state.h (extracted in brief X.7.1 alongside the Viewport struct).


// X.7.9: ParsedSettings + the settings parse / format / write helpers
// moved to settings_io.{h,cpp} so file_loader.cpp and save_markers can
// both reach them.

// X.7.8a: WaveformCache was promoted to paint_handler.{h,cpp} so
// paint_handler.cpp can reach it. The instance is still a local in
// main() and is passed by reference into GuiPaintHandler.

} // namespace

// Geometry helpers — public to viewport.cpp via app_state.h. The strip-height
// helpers and samples_per_pixel_at remain main-private (`static`).

static int top_strip_height(int window_height) {
    return static_cast<int>(std::lround(window_height * kTopStripRatio));
}

static int bottom_strip_height(int window_height) {
    return static_cast<int>(std::lround(window_height * kBottomStripRatio));
}

GuiRect waveform_area(const AppState& a) {
    const int top_h = top_strip_height(a.height);
    const int bot_h = bottom_strip_height(a.height);
    return GuiRect{0, top_h, a.width, a.height - top_h - bot_h};
}

GuiRect top_strip_area(const AppState& a) {
    return GuiRect{0, 0, a.width, top_strip_height(a.height)};
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

    const ViewState& vs = active_view_state(a);
    if (vs.has_trim_begin) {
        begin = static_cast<long long>(
            std::nearbyint(vs.trim_begin_seconds * sr));
    }
    if (vs.has_trim_end) {
        end = static_cast<long long>(
            std::nearbyint(vs.trim_end_seconds * sr));
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
    if (a.view_domain == ViewDomain::Target &&
        !a.render_view_enabled &&
        a.target_view_total_frames > 0) {
        return a.target_view_total_frames;
    }
    return audio.total_frames();
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

double playhead_pixel_x(const AppState& a, const GuiAudio& audio) {
    const double spp = current_samples_per_pixel(a, audio);
    if (spp <= 0.0) return -1.0;
    return static_cast<double>(a.playhead_sample - a.viewport_start_sample) / spp;
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
    // the chunk-P triangle indicator in the flag strip above it.
    const int y0 = 0;
    const int y1 = area.y + area.h;
    return GuiRect{x0, y0, x1 - x0, y1 - y0};
}

GuiRect timestamp_invalidate_rect(int window_height, int window_width,
                                  bool wide_strip) {
    if (wide_strip) {
        return GuiRect{0, window_height - kTimestampRegionH,
                       window_width, kTimestampRegionH};
    }
    return GuiRect{0, window_height - kTimestampRegionH,
                   kTimestampRegionW, kTimestampRegionH};
}


int main(int argc, char** argv) {
    const char* cli_path = nullptr;
    if (argc == 1) {
        // Empty window; wait for a drag-and-drop.
    } else if (argc == 2) {
        cli_path = argv[1];
    } else {
        std::fprintf(stderr, "usage: warptempo_gui [<audio_file>]\n");
        return 1;
    }

    AppState     app;
    GuiAudio     audio;
    GuiPlayback  playback;
    GuiPlatform  gui;
    WaveformCache wf_cache;
    if (!gui.init(app.width, app.height, "Warptempo")) {
        return 1;
    }

    // -- Viewport + invalidation helpers ------------------------------------
    //
    // X.7.1: the viewport-mutator and invalidation lambdas have been hoisted
    // onto the Viewport struct in viewport.{cpp,h}. The lambdas below are
    // one-line forwarders so callsites elsewhere in main() don't need to
    // change. `invalidate_timestamp_area` is gone — its body was
    // byte-identical to invalidate_timestamp_area, so all of its former
    // callsites now call invalidate_timestamp_area directly. `bottom_strip
    // _wide` was promoted to a free function in app_state.{h,cpp} in
    // X.7.8a so paint_handler.cpp can reach it without a capture.
    //
    // X.7.13: the nine std::function forward-declares previously kept here
    // (recompute_hover_at_cursor, clear_hover_popup, stop_playback_if_playing,
    // refresh_active_tab_from_app, save_markers, request_close_or_revert,
    // prompt_activate_response, toggle_playback, set_playback_speed) were
    // retired. The two substantive bodies moved onto Viewport
    // (clear_hover_popup, recompute_hover_at_cursor); the seven forwarders
    // are now direct method calls on their owning struct
    // (viewport.clear_hover_popup, playback_lifecycle.stop_playback_if_playing
    // / toggle_playback / set_playback_speed, save_ops.save,
    // prompt.request_close_or_revert / activate_response,
    // tab_mode.refresh_active_tab_from_app).

    Viewport viewport(app, audio, gui, playback);
    GuiPlaybackLifecycle playback_lifecycle(app, audio, gui, playback, viewport);
    GuiFileLoader file_loader(app, audio, gui, playback, wf_cache, viewport);
    Selection selection(app, audio, viewport, playback);
    GuiAsyncRenderer async_renderer;
    if (!async_renderer.init()) {
        std::fprintf(stderr,
            "warptempo_gui: failed to start async renderer; exiting\n");
        return 1;
    }
    // GuiTargetRender is the cancel-restart dispatcher for target-view
    // live audio. It must be constructed after async_renderer
    // (a dependency) and BEFORE the op clusters (which take it as a
    // ref). The trigger() method is a no-op in source view, so injecting
    // it into source-view-only call sites is harmless.
    GuiTargetRender target_render(app, audio, async_renderer, playback,
                                  viewport);
    GuiTabMode tab_mode(app, audio, viewport, selection,
                        playback_lifecycle, target_render);
    Undo undo(app, viewport, selection, playback_lifecycle, tab_mode,
              target_render);
    GuiPhaseResetMarkersOps phase_resets(app, audio, viewport, selection, undo,
                                         playback_lifecycle, target_render);
    GuiWarpMarkersOps warpops(app, audio, gui, viewport, selection, undo,
                              playback_lifecycle, target_render);
    GuiFlagEditor flag_editor(app, audio, viewport, selection, undo,
                              target_render);
    GuiRenderView render_view(app, audio, playback, gui, selection,
                              viewport, tab_mode);
    GuiPaintHandler paint_handler(app, audio, playback, wf_cache, gui);
    PhaseResetPropagate phase_reset_propagate(app, viewport, undo,
                                              target_render);
    GuiSaveOps save_ops(app, undo, tab_mode, viewport);
    GuiPrompt prompt(app, gui, viewport, file_loader,
                     phase_reset_propagate, save_ops);
    GuiSettingsEditor settings_editor(app, audio, viewport, tab_mode, undo,
                                      target_render);
    gui.set_worker_completion_fd(async_renderer.completion_fd(),
        [&async_renderer]() { async_renderer.on_completion_event(); });
    GuiInputHandler input_handler(app, audio, gui, playback,
                                  viewport, selection, undo,
                                  warpops, phase_resets, flag_editor,
                                  render_view, tab_mode,
                                  phase_reset_propagate,
                                  async_renderer,
                                  playback_lifecycle, save_ops, prompt,
                                  settings_editor, target_render);

    auto invalidate_timestamp_area   = [&]() { viewport.invalidate_timestamp_area(); };
    auto invalidate_playhead_columns = [&](double a, double b) { viewport.invalidate_playhead_columns(a, b); };
    auto follow_scroll_if_needed     = [&]() { viewport.follow_scroll_if_needed(); };

    // -- Redraw -------------------------------------------------------------

    gui.set_on_redraw([&](cairo_t* cr, int x, int y, int w, int h) {
        paint_handler.on_redraw(cr, x, y, w, h);
    });

    gui.set_on_resize([&](int w, int h) {
        paint_handler.on_resize(w, h);
    });

    auto invalidate_top_strip     = [&]() { viewport.invalidate_top_strip(); };

    // X.7.8b-3: popup_eligible_marker moved to a free function in
    // app_state.{h,cpp}. The remaining callers in this TU
    // (Viewport::recompute_hover_at_cursor, on_tick) reach it directly
    // with the new (app, idx) signature; on_motion calls it from
    // input_handler.cpp. V.A3b / V.B comments live above the
    // declaration in app_state.h.

    // X.7.5a: the drag and selection-shift lambdas have been hoisted onto
    // the GuiWarpMarkersOps struct in warpmarkers_ops.{cpp,h}.

    // X.7.8b-2: the shared wheel handler (handle_wheel) moved to
    // GuiInputHandler as a private helper method. on_button_press is
    // its only caller after this brief.

    // X.7.8b-1: the multi-render queue runner (run_render_batch +
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

    gui.set_on_motion([&](int mouse_x, int mouse_y, GuiInputState mods) {
        input_handler.on_motion(mouse_x, mouse_y, mods);
    });

    // -- File loading --------------------------------------------------------
    //
    // X.7.9: load_file, revert_to_blank, and load_then_drain moved to
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
        // Blink the editor cursor independently of playback. Compare the
        // current visibility against the last painted state and invalidate
        // the top strip when it flips. Cheap: top_strip is small.
        if (text_editor::is_active(app.top_flag_editor)) {
            const bool now_visible =
                text_editor::cursor_visible_now(app.top_flag_editor);
            if (now_visible != app.top_flag_editor_blink_last) {
                app.top_flag_editor_blink_last = now_visible;
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

        // V.A3b: dwell-driven popup show. The motion handler already gates
        // on warp mode + no editor + no drag + no dialog and clears
        // hover_popup when those conditions break, so here it's enough to
        // check the elapsed time and re-validate eligibility.
        if (!app.hover_popup.visible &&
            app.hover_popup.marker_index >= 0 &&
            popup_eligible_marker(app, app.hover_popup.marker_index)) {
            const auto now = std::chrono::steady_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - app.hover_popup.entry_time).count();
            if (ms >= kHoverDelayMs) {
                app.hover_popup.visible = true;
                invalidate_top_strip();
            }
        }

        if (app.loading || audio.total_frames() <= 0) return;

        const bool ma_playing = playback.is_playing();
        if (!app.is_playing && !ma_playing) return;

        if (ma_playing) {
            // Heartbeat: invalidate the playhead column at the current
            // model position so the paint cycle keeps running. The
            // pre-paint hook reads the predictor at paint time and adds
            // damage for the actually-painted position. We do not read
            // the predictor or update app.playhead_sample here — that
            // work moved to the pre-paint hook to eliminate the
            // tick/paint sampling-rate mismatch that caused playhead
            // motion to stutter at high zoom. The timestamp area is
            // invalidated only by the pre-paint hook (when the
            // predictor advances past app.playhead_sample), never by
            // the tick — the tick fires ~2x per frame, so duplicating
            // the timestamp rect here is wasted on_redraw work.
            const double px = playhead_pixel_x(app, audio);
            invalidate_playhead_columns(px, px);
            app.is_playing = true;
            return;
        }

        // Playing was true last tick, now false — natural end. Return the
        // visible playhead to the launch position (same as Space-to-stop).
        if (app.is_playing) {
            playback_lifecycle.restore_playhead_to_lsp();
            if (app.follow_mode) follow_scroll_if_needed();
        }
    });

    gui.set_on_pre_paint([&]() {
        if (app.loading || audio.total_frames() <= 0) return;
        if (!playback.is_playing()) return;

        // Read the predictor at paint time. The predictor is continuous
        // in wall time, so this gives the freshest possible position
        // right before paint consumes the damage list.
        const int64_t cur = playback.cursor();
        // Target view: playback.cursor() is a target-buffer-frame
        // index in [0, target_buffer_frames). app.playhead_sample is
        // full-target-frame. Translate at the boundary. Source view and
        // render view: identity (impl_->samples points at the source's
        // own audio for both; cursor is already in the active-domain
        // coordinate system). The target_buffer_frames > 0 guard
        // ensures the bias is only applied when a successful target
        // render has populated the buffer; pre-paint can fire briefly
        // between dispatch and on_render_done if a paint races a
        // cancel, and applying a stale bias against the source-bound
        // buffer would skew the playhead.
        int64_t translated = cur;
        if (app.view_domain == ViewDomain::Target &&
            !app.render_view_enabled &&
            app.target_buffer_frames > 0) {
            translated = cur + app.target_buffer_start_frame;
        }
        if (translated == app.playhead_sample) return;

        const double old_px = playhead_pixel_x(app, audio);
        app.playback_cursor  = translated;
        app.playhead_sample  = translated;
        const double new_px  = playhead_pixel_x(app, audio);

        // invalidate_region during pre-paint appends to damage_ without
        // scheduling a redundant frame callback (platform layer handles
        // that via its in_pre_paint_ flag).
        invalidate_playhead_columns(old_px, new_px);
        invalidate_timestamp_area();
        if (app.follow_mode) follow_scroll_if_needed();
    });

    // Paint the initial background before any synchronous load begins so the
    // window isn't briefly blank on fast disks.
    gui.invalidate_region(0, 0, app.width, app.height);
    gui.drain_events();

    if (cli_path) {
        if (!file_loader.load_file(cli_path)) {
            gui.shutdown();
            return 1;
        }
        // Any drops queued during the startup load run now.
        while (!app.pending_drop_path.empty()) {
            std::string next = std::move(app.pending_drop_path);
            app.pending_drop_path.clear();
            file_loader.load_file(next);
        }
    }

    gui.run();
    // Tear the audio device down before the sample buffer goes out of scope.
    playback.shutdown();
    gui.shutdown();
    return 0;
}
