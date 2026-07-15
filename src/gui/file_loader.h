#pragma once

#include "app_state.h"
#include "audio.h"
#include "paint_handler.h"   // for GuiPaintHandler
#include "playback.h"
#include "platform_wayland.h"
#include "viewport.h"

#include <functional>
#include <string>
#include <thread>

struct GuiTargetRender;

// File-lifecycle operations, extracted from main.cpp's inline
// lambdas. Owns the audio-load → markers-parse → settings-parse →
// playback-init pipeline (load_file), the sole loader, invoked once from
// the startup tick: the GUI has no in-session file open or drag-and-drop,
// so a failed load exits rather than reverting to a blank window.
struct GuiFileLoader {
    AppState&          app;
    GuiAudio&          audio;
    GuiPlatform&       gui;
    GuiPlayback&       playback;
    Viewport&          viewport;
    GuiTargetRender&   target_render;
    // Loading a source can change app.font_size; the load applies it
    // through GuiPaintHandler::on_resize, the same geometry-and-cache
    // rebuild path a window resize takes.
    GuiPaintHandler&   paint_handler;

    GuiFileLoader(AppState&          app_,
                  GuiAudio&          audio_,
                  GuiPlatform&       gui_,
                  GuiPlayback&       playback_,
                  Viewport&          viewport_,
                  GuiTargetRender&   target_render_,
                  GuiPaintHandler&   paint_handler_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_),
          viewport(viewport_),
          target_render(target_render_),
          paint_handler(paint_handler_) {}

    ~GuiFileLoader();

    bool load_file(const std::string& path);
    void join_source_sample_cache_writer();

    // The launch-only writer captures the source buffer by shared_ptr
    // (samples_shared()), owning a lifetime-safe view of its own. There is no
    // in-session replacement load, so the destructor join is the surviving
    // synchronization before GuiAudio is destroyed.
    std::thread source_sample_cache_writer_;
};

// Apply a parsed settings file's engine block and the scalar session prefs
// (follow, active_audio_view, active_markers_view, active_tab_view,
// playback_speed, font_size, audio_player) into `app`. VALUES ONLY — no side
// effects: the caller runs set_speed / set_gui_font_size_pt / on_resize itself,
// so both callers (load_file and the render-entry adopt) apply these fields
// identically while owning their own side-effect timing. SettingsFile is
// visible here via app_state.h -> settings_file.h.
void apply_settings_engine_and_prefs(AppState& app, const SettingsFile& sf);
