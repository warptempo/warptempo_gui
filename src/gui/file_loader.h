#pragma once

#include "app_state.h"
#include "audio.h"
#include "paint_handler.h"   // for WaveformCache
#include "playback.h"
#include "platform_wayland.h"
#include "viewport.h"

#include <string>

struct GuiTargetRender;

// X.7.9: file-lifecycle operations, extracted from main.cpp's inline
// lambdas. Owns the audio-load → markers-parse → settings-parse →
// playback-init pipeline (load_file) and its companion revert-to-blank
// operation. Both manipulate the same per-file AppState fields and the
// same audio device. The drop-accept predicate and the on_file_drop
// handler are supplied to the platform layer as std::function objects
// produced by methods on this struct, so the platform wiring in main.cpp
// is a single line each.
struct GuiFileLoader {
    AppState&        app;
    GuiAudio&        audio;
    GuiPlatform&     gui;
    GuiPlayback&     playback;
    WaveformCache&   wf_cache;
    Viewport&        viewport;
    GuiTargetRender& target_render;

    GuiFileLoader(AppState&        app_,
                  GuiAudio&        audio_,
                  GuiPlatform&     gui_,
                  GuiPlayback&     playback_,
                  WaveformCache&   wf_cache_,
                  Viewport&        viewport_,
                  GuiTargetRender& target_render_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_),
          wf_cache(wf_cache_),
          viewport(viewport_),
          target_render(target_render_) {}

    bool load_file(const std::string& path);
    void revert_to_blank();
    void load_then_drain(std::string path);
};
