#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "warpmarkers.h"
#include "file_loader.h"
#include "flag_editor.h"
#include "input_handler.h"
#include "paint_handler.h"
#include "playback.h"
#include "render.h"
#include "render_pipeline.h"
#include "render_view.h"
#include "selection.h"
#include "settings_io.h"
#include "tab_mode.h"
#include "text_display.h"
#include "text_editor.h"
#include "time_format.h"
#include "phase_reset_markers.h"
#include "phase_reset_markers_ops.h"
#include "undo.h"
#include "viewport.h"
#include "warpmarkers_ops.h"
#include "platform_wayland.h"

#include <cairo/cairo.h>
#include <sndfile.h>

#include <algorithm>
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
#include <functional>
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

// ms-per-pixel for each numeric zoom level. Level 0 is most zoomed in.
// kNumZoomLevels (in app_state.h) is the count of entries here; the
// static_assert below pins them together so the table can't drift.
constexpr double kZoomMsPerPixel[] = {
    1.25, 2.6, 5.2, 10.4, 20.8, 41.7, 83.3, 166.7
};
static_assert(sizeof(kZoomMsPerPixel) / sizeof(kZoomMsPerPixel[0])
              == static_cast<size_t>(kNumZoomLevels),
              "kZoomMsPerPixel size must match kNumZoomLevels");

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

    if (a.has_trim_begin) {
        begin = static_cast<long long>(
            std::nearbyint(a.trim_begin_seconds * sr));
    }
    if (a.has_trim_end) {
        end = static_cast<long long>(
            std::nearbyint(a.trim_end_seconds * sr));
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
    return kZoomMsPerPixel[zoom_level] *
           static_cast<double>(sample_rate) / 1000.0;
}

// Largest numeric level L (in [0, kNumZoomLevels)) whose samples_visible does
// not exceed total_frames. Returns -1 if even level 0 shows more than the
// file — in which case fit-file is the only valid level.
int max_valid_numeric_level(int waveform_width_px,
                            int64_t total_frames,
                            int sample_rate) {
    int best = -1;
    for (int L = 0; L < kNumZoomLevels; L++) {
        const double spp =
            samples_per_pixel_at(L, waveform_width_px, total_frames, sample_rate);
        const double visible = spp * waveform_width_px;
        if (visible <= static_cast<double>(total_frames)) best = L;
        else break; // table is monotonic
    }
    return best;
}

int64_t samples_visible(const AppState& a, const GuiAudio& audio) {
    const GuiRect area = waveform_area(a);
    const double spp = samples_per_pixel_at(
        a.zoom_level, area.w, audio.total_frames(), audio.sample_rate());
    return static_cast<int64_t>(std::nearbyint(spp * area.w));
}

double current_samples_per_pixel(const AppState& a, const GuiAudio& audio) {
    const GuiRect area = waveform_area(a);
    return samples_per_pixel_at(
        a.zoom_level, area.w, audio.total_frames(), audio.sample_rate());
}

// Applies a position delta (in seconds) to the phase reset's time_seconds.
void apply_phase_reset_position_delta(GuiPhaseResetMarker& m, double delta_seconds) {
    if (delta_seconds == 0.0) return;
    m.time_seconds += delta_seconds;
}

void clamp_viewport_start(AppState& a, const GuiAudio& audio) {
    const int64_t visible = samples_visible(a, audio);
    const int64_t total   = audio.total_frames();
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

    // V.A3b Addendum 3: forward-declared so the viewport methods below can
    // invoke it. The body is assigned later (after clear_hover_popup is in
    // scope; X.7.8b-2 made hit_test_flag a free function in app_state.{h,
    // cpp} so it's no longer a scope concern). Guarded inside Viewport
    // with a truthiness check because callbacks are wired after this
    // assignment.
    std::function<void()> recompute_hover_at_cursor;

    // X.7.3: forward-declared so the Undo struct can capture references.
    // Bodies are assigned later at their original definition sites — same
    // forward-declare-then-assign pattern as recompute_hover_at_cursor.
    std::function<void()> clear_hover_popup;
    std::function<void()> stop_playback_if_playing;

    // X.7.6: forward-declared so GuiRenderView can capture a reference.
    // Body is assigned later at its original definition site — same
    // pattern as the four prior promotions.
    std::function<void()> refresh_active_tab_from_app;

    // X.7.8b-1: forward-declared so GuiInputHandler can capture references.
    // Bodies are assigned later at their original definition sites — same
    // pattern as the prior promotions. The on_key handler reads these
    // through the std::function refs; the residual lambdas stay in
    // main.cpp because their bodies still reach lambdas that have not yet
    // been hoisted (proceed_with_trigger, open_prompt_unsaved, etc.).
    std::function<bool()>                save_markers;
    std::function<void(DialogTrigger)>   request_close_or_revert;
    std::function<void(char)>            prompt_activate_response;
    std::function<void()>                toggle_playback;
    std::function<void(float)>           set_playback_speed;

    Viewport viewport(app, audio, gui, playback, recompute_hover_at_cursor);
    GuiFileLoader file_loader(app, audio, gui, playback, wf_cache, viewport);
    Selection selection(app, audio, viewport, playback);
    Undo undo(app, viewport, selection,
              clear_hover_popup, stop_playback_if_playing);
    GuiPhaseResetMarkersOps phase_resets(app, audio, viewport, selection, undo,
                                      clear_hover_popup, stop_playback_if_playing);
    GuiWarpMarkersOps warpops(app, audio, gui, viewport, selection, undo,
                              clear_hover_popup, stop_playback_if_playing);
    GuiFlagEditor flag_editor(app, audio, viewport, selection, undo,
                              clear_hover_popup);
    GuiRenderView render_view(app, audio, playback, gui, selection,
                              clear_hover_popup, refresh_active_tab_from_app);
    GuiTabMode tab_mode(app, audio, viewport, selection,
                        clear_hover_popup, stop_playback_if_playing);
    GuiPaintHandler paint_handler(app, audio, playback, wf_cache, gui);
    PhaseResetPropagate phase_reset_propagate(app, viewport, undo);
    GuiAsyncRenderer async_renderer;
    if (!async_renderer.init()) {
        std::fprintf(stderr,
            "warptempo_gui: failed to start async renderer; exiting\n");
        return 1;
    }
    gui.set_worker_completion_fd(async_renderer.completion_fd(),
        [&async_renderer]() { async_renderer.on_completion_event(); });
    GuiInputHandler input_handler(app, audio, gui, playback,
                                  viewport, selection, undo,
                                  warpops, phase_resets, flag_editor,
                                  render_view, tab_mode,
                                  phase_reset_propagate,
                                  async_renderer,
                                  clear_hover_popup, stop_playback_if_playing,
                                  save_markers, request_close_or_revert,
                                  prompt_activate_response, toggle_playback,
                                  set_playback_speed);

    auto trim_end_sample             = [&]() { return viewport.trim_end_sample(); };
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
    // (recompute_hover_at_cursor below, on_tick) reach it directly with
    // the new (app, idx) signature; on_motion calls it from
    // input_handler.cpp. V.A3b / V.B comments live above the
    // declaration in app_state.h.

    // Reset the hover popup state. If the popup was visible, invalidate the
    // top strip so the next paint erases it. Safe to call from any path.
    clear_hover_popup = [&]() {
        const bool was_visible = app.hover_popup.visible;
        app.hover_popup = HoverPopupState{};
        if (was_visible) invalidate_top_strip();
    };

    // -- Undo/redo helpers --------------------------------------------------
    //
    // X.7.3: the undo-cluster lambdas have been hoisted onto the Undo struct
    // in undo.{cpp,h}. The lambdas below are one-line forwarders so callsites
    // elsewhere in main() don't need to change. apply_post_restore_rules_warp
    // and apply_post_restore_rules_phase_reset have no callers outside the
    // undo cluster, so their forwarders are dropped — they remain public on
    // the Undo struct for consistency.

    auto recompute_dirty = [&]() { undo.recompute_dirty(); };

    // Gesture-stop: called at the top of any handler that will move the
    // visible playhead (keys, button press, Ctrl+wheel, undo/redo, tab
    // switch). Stops the audio thread and keeps the LSP in sync with the
    // visible playhead so the next Space-to-play captures the right
    // launch position. Does NOT return-to-launch — the gesture is about
    // to commit a new playhead position.
    stop_playback_if_playing = [&]() {
        if (!playback.is_playing() && !app.is_playing) return;
        playback.stop();
        app.is_playing        = false;
        app.last_space_sample = app.playhead_sample;
    };

    refresh_active_tab_from_app = [&]() { tab_mode.refresh_active_tab_from_app(); };

    save_markers = [&]() -> bool {
        if (app.warpmarkers_path.empty()) return false;
        if (app.first_save_pending && app.warpmarkers.had_nonstandard_content()) {
            std::fprintf(stderr,
                "warptempo_gui: first save in this session will discard "
                "comments and freeform text from %s. Canonical format will "
                "be written.\n",
                app.warpmarkers_path.c_str());
        }
        // Capture the active tab's current values before any writes — both
        // the .warpmarkers and .settings paths see a consistent snapshot.
        refresh_active_tab_from_app();

        const bool ok = app.warpmarkers.save(app.warpmarkers_path);
        if (!ok) {
            std::fprintf(stderr,
                "warptempo_gui: save failed: %s\n",
                app.warpmarkers_path.c_str());
            return false;
        }

        // Phase resets sibling write. Empty list deletes the on-disk file so
        // a project never carries a stale empty .phaseresetmarkers.
        if (!app.phase_reset_markers_path.empty()) {
            if (app.phase_reset_markers.markers().empty()) {
                if (!app.phase_reset_markers.delete_file(app.phase_reset_markers_path)) {
                    std::fprintf(stderr,
                        "warptempo_gui: failed to delete: %s\n",
                        app.phase_reset_markers_path.c_str());
                }
            } else {
                if (!app.phase_reset_markers.save(app.phase_reset_markers_path)) {
                    std::fprintf(stderr,
                        "warptempo_gui: phase_reset save failed: %s\n",
                        app.phase_reset_markers_path.c_str());
                    return false;
                }
            }
        }

        app.first_save_pending = false;
        // Save rebinds the saved reference to the current timeline position
        // without touching either stack — undo still reverts the last op.
        const bool was_dirty = app.dirty;
        app.history.mark_saved();
        recompute_dirty();
        if (was_dirty != app.dirty) {
            invalidate_timestamp_area();
        }

        // Best-effort .settings write. Failure is logged but does not fail
        // the overall save — the .warpmarkers write is the primary target.
        if (!app.settings_path.empty()) {
            if (!write_settings_file(app.settings_path,
                                     app.tab_a, app.tab_b,
                                     app.follow_mode,
                                     app.active_mode,
                                     app.playback_speed,
                                     app.has_trim_begin, app.trim_begin_seconds,
                                     app.has_trim_end,   app.trim_end_seconds,
                                     app.settings_passthrough)) {
                std::fprintf(stderr,
                    "warptempo_gui: settings save failed: %s: %s\n",
                    app.settings_path.c_str(),
                    std::strerror(errno));
            } else {
                // Successful settings write clears the settings-side
                // dirty flag and refolds it into app.dirty.
                if (app.settings_dirty) {
                    app.settings_dirty = false;
                    app.dirty = app.warp_dirty || app.phase_reset_dirty;
                    invalidate_timestamp_area();
                }
            }
        }
        return true;
    };

    // Snap the visible playhead back to where Space was last pressed and
    // refresh the affected regions. Used by both Space-to-stop and natural
    // end-of-playback.
    auto restore_playhead_to_lsp = [&]() {
        const double old_px = playhead_pixel_x(app, audio);
        app.playhead_sample = app.last_space_sample;
        const double new_px = playhead_pixel_x(app, audio);
        invalidate_playhead_columns(old_px, new_px);
        invalidate_timestamp_area();
        // The triangle shares the top strip with any selected-flag
        // highlight; restore jumps can uncover/cover both, so invalidate
        // the flag strip too.
        const GuiRect ts = top_strip_area(app);
        gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
        app.is_playing      = false;
        app.playback_cursor = app.playhead_sample;
    };

    // -- Unsaved-work dialog + blank-state revert (chunk Q) -----------------

    auto invalidate_all = [&]() { viewport.invalidate_all(); };

    // X.7.9: revert_to_blank moved to file_loader.{h,cpp} on GuiFileLoader.
    // proceed_with_trigger's REVERT_TO_BLANK case dispatches through it.

    auto proceed_with_trigger = [&](DialogTrigger t) {
        switch (t) {
        case DialogTrigger::CLOSE_WINDOW:
            gui.request_exit();
            break;
        case DialogTrigger::REVERT_TO_BLANK:
            file_loader.revert_to_blank();
            break;
        case DialogTrigger::PASTE_CONFIRM:
            // Paste prompt is dispatched directly by prompt_activate_response;
            // proceed_with_trigger is not the path it lands on.
            break;
        }
    };

    auto open_prompt_unsaved = [&](DialogTrigger t) {
        app.prompt.active          = true;
        app.prompt.text            = "Save unsaved changes?";
        // Sentinel chars for non-letter keys: 0x7F = Delete, 0x1B = Escape.
        // The GuiKey → char mapping in input_handler.cpp's prompt dispatch
        // produces these for GuiKeys::Delete / GuiKeys::Escape; the prompt
        // machinery remains a vector<char> match.
        app.prompt.response_keys   = {'s', '\x7f', '\x1b'};
        app.prompt.response_labels = {"[S]ave", "[Delete]", "[Esc]"};
        app.prompt.trigger         = t;
        clear_hover_popup();
        invalidate_all();
    };

    // Single-key response dispatch. The trigger captured at prompt-open
    // time selects which response set is in play; the key picks the
    // response. On a Save failure, the prompt mutates in place to a
    // retry/discard/cancel state — same trigger, new text and response
    // set — rather than dismissing.
    prompt_activate_response = [&](char k) {
        if (!app.prompt.active) return;
        const DialogTrigger trigger = app.prompt.trigger;
        // Sentinels: '\x7f' = Delete (discard), '\x1b' = Escape (cancel).
        // See open_prompt_unsaved above.

        if (trigger == DialogTrigger::PASTE_CONFIRM) {
            if (k == 'y') {
                app.prompt.active = false;
                invalidate_all();
                phase_reset_propagate.paste_apply();
                return;
            }
            if (k == '\x1b') {
                app.prompt.active = false;
                app.pending_paste_anchor = -1;
                invalidate_all();
                return;
            }
            return;
        }

        if (trigger == DialogTrigger::CLOSE_WINDOW ||
            trigger == DialogTrigger::REVERT_TO_BLANK) {
            if (k == 's' || k == 'r') {
                const bool ok = save_markers();
                if (!ok) {
                    app.prompt.text            = "Save failed.";
                    app.prompt.response_keys   = {'r', '\x7f', '\x1b'};
                    app.prompt.response_labels =
                        {"[R]etry", "[Delete]", "[Esc]"};
                    invalidate_all();
                    return;
                }
                app.prompt.active = false;
                invalidate_all();
                proceed_with_trigger(trigger);
                return;
            }
            if (k == '\x7f') {
                app.prompt.active = false;
                invalidate_all();
                proceed_with_trigger(trigger);
                return;
            }
            if (k == '\x1b') {
                app.prompt.active = false;
                invalidate_all();
                return;
            }
            return;
        }
    };

    // Route a close / revert gesture through the prompt when history is
    // dirty; otherwise proceed immediately. Centralizes the decision so
    // Ctrl+Q, Ctrl+W, and the WM-close callback share identical behavior.
    request_close_or_revert = [&](DialogTrigger t) {
        if (app.prompt.active) return; // already gated; ignore re-entry
        if (app.dirty) open_prompt_unsaved(t);
        else           proceed_with_trigger(t);
    };

    // Space-bar: start/stop playback. Playback runs from the playhead to
    // trim_end (or total_frames if no e= marker). Pressing space with the
    // playhead at or past trim-end is a silent no-op. Space-to-stop
    // returns the visible playhead to the position where Space-to-play
    // was last pressed (return-to-launch).
    toggle_playback = [&]() {
        if (playback.is_playing()) {
            playback.stop();
            restore_playhead_to_lsp();
            return;
        }
        const int64_t end = trim_end_sample();
        if (app.playhead_sample >= end) return;
        // Clamp the start position into the trim range in case the playhead
        // is sitting at trim_end - 1 (valid) or somehow slipped.
        const int64_t start = std::max(app.playhead_sample, viewport.trim_begin_sample());
        app.last_space_sample = app.playhead_sample;
        app.playback_cursor = start;
        app.is_playing = true;
        if (app.follow_mode) follow_scroll_if_needed();
        playback.set_speed(app.render_view_enabled ? 1.0f : app.playback_speed);
        playback.play(start, end);
    };

    // Helpers for Shift+<digit> speed selection.
    set_playback_speed = [&](float s) {
        app.playback_speed = s;
        playback.set_speed(s);
        // Speed change without resync would cause a backward cursor jump:
        // the predictor would retroactively apply the new speed to the
        // entire elapsed-since-anchor period.
        if (playback.is_playing()) playback.resync_predictor();
    };

    // V.A3b Addendum 3: re-evaluate hover at the cursor's last on_motion
    // coordinates. Called after viewport mutations (zoom, scroll, center,
    // playhead-driven viewport shift) so a stationary cursor's hover state
    // tracks the rects that just slid under it. Mirrors the on_motion
    // hover-detection branch: same gating, same hit-test, same state
    // transitions; the tick handler still drives the dwell-to-visible flip.
    recompute_hover_at_cursor = [&]() {
        if (app.last_mouse_x < 0 || app.last_mouse_y < 0) return;
        // Dialog / drag / editor / queue still suppress hover in either
        // view. Source-view also requires warp mode + iter mode off;
        // render-view bypasses the mode checks because hover always
        // applies against the loaded render's warpmarkers.
        if (app.prompt.active ||
            app.drag.active ||
            app.playhead_drag.active ||
            text_editor::is_active(app.top_flag_editor) ||
            app.queue_running) {
            clear_hover_popup();
            return;
        }
        if (!app.render_view_enabled &&
            (app.active_mode != 'W' || app.iteration_mode_enabled)) {
            clear_hover_popup();
            return;
        }
        const int hit = hit_test_flag(app, audio,
                                      app.last_mouse_x, app.last_mouse_y);
        if (hit != app.hover_popup.marker_index) {
            if (app.hover_popup.visible) invalidate_top_strip();
            app.hover_popup.marker_index = hit;
            app.hover_popup.visible      = false;
            app.hover_popup.entry_time   =
                std::chrono::steady_clock::now();
            // Precompute the popup's display text at rect-entry so the
            // delay-completion paint doesn't have to recompute it. Empty
            // when `hit` is not popup-eligible (the redraw branch then
            // skips paint and keeps the strip clean).
            if (app.render_view_enabled) {
                app.hover_popup.cached_text =
                    popup_eligible_marker(app, hit)
                        ? compute_hover_popup_text(
                              app.render_view_markers, hit,
                              app.render_view_src_sr)
                        : std::string();
            } else {
                app.hover_popup.cached_text =
                    popup_eligible_marker(app, hit)
                        ? compute_hover_popup_text(
                              app.warpmarkers.markers(), hit,
                              audio.sample_rate())
                        : std::string();
            }
        }
    };

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
        request_close_or_revert(DialogTrigger::CLOSE_WINDOW);
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
            restore_playhead_to_lsp();
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
        if (cur == app.playhead_sample) return;

        const double old_px = playhead_pixel_x(app, audio);
        app.playback_cursor  = cur;
        app.playhead_sample  = cur;
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
