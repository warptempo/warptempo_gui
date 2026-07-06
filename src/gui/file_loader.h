#pragma once

#include "app_state.h"
#include "audio.h"
#include "paint_handler.h"   // for WaveformCache
#include "playback.h"
#include "platform_wayland.h"
#include "viewport.h"

#include <string>
#include <thread>

class GuiWaveformWorker;
struct GuiTargetRender;

// File-lifecycle operations, extracted from main.cpp's inline
// lambdas. Owns the audio-load → markers-parse → settings-parse →
// playback-init pipeline (load_file) and its companion revert-to-blank
// operation. Both manipulate the same per-file AppState fields and the
// same audio device. The drop-accept predicate and the on_file_drop
// handler are supplied to the platform layer as std::function objects
// produced by methods on this struct, so the platform wiring in main.cpp
// is a single line each.
struct GuiFileLoader {
    AppState&          app;
    GuiAudio&          audio;
    GuiPlatform&       gui;
    GuiPlayback&       playback;
    WaveformCache&     wf_cache;
    StemCache&         stem_cache;
    FlagCache&         flag_cache;
    GuiWaveformWorker& waveform_worker;
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
                  WaveformCache&     wf_cache_,
                  StemCache&         stem_cache_,
                  FlagCache&         flag_cache_,
                  GuiWaveformWorker& waveform_worker_,
                  Viewport&          viewport_,
                  GuiTargetRender&   target_render_,
                  GuiPaintHandler&   paint_handler_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_),
          wf_cache(wf_cache_),
          stem_cache(stem_cache_),
          flag_cache(flag_cache_),
          waveform_worker(waveform_worker_),
          viewport(viewport_),
          target_render(target_render_),
          paint_handler(paint_handler_) {}

    ~GuiFileLoader();

    bool load_file(const std::string& path);
    void revert_to_blank();
    void load_then_drain(std::string path);
    void join_source_sample_cache_writer();

    // The writer reads GuiAudio's sample buffer through a raw pointer.
    // Callers join before replacing the buffer in load_file/revert_to_blank,
    // and the destructor joins before GuiAudio is destroyed.
    std::thread source_sample_cache_writer_;
};
