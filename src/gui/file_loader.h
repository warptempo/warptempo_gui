#pragma once

#include "app_state.h"
#include "audio.h"
#include "paint_handler.h"   // for WaveformCache
#include "playback.h"
#include "platform_wayland.h"
#include "viewport.h"

#include <functional>
#include <string>
#include <thread>

class GuiWaveformWorker;
struct GuiTargetRender;
struct GuiPrompt;

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

    // Back-pointer, wired in main.cpp right after GuiPrompt's construction
    // (GuiPrompt holds a GuiFileLoader&, so it is necessarily built after
    // this struct). Carries the dismiss-only error notice for a private
    // cache file opened by mistake. Non-null for the whole run loop: every
    // load path — the deferred startup load included — runs on ticks, after
    // the wiring.
    GuiPrompt* prompt = nullptr;

    // Render-view teardown hook, wired in main.cpp after GuiRenderView's
    // construction (GuiRenderView is built after this struct, so the
    // reference cannot be a constructor parameter — the same
    // post-construction back-wire shape as `prompt` above). Bound to
    // GuiRenderView::abandon_render_view: load_file calls it early, while
    // the source audio is still alive, so playback is rebound to the source
    // and the entry buffer freed before the source is installed. On the sole
    // startup load there is no render view up, so the call no-ops.
    std::function<void()> abandon_render_view;

    // Archival-session cancel hook, wired in main.cpp after
    // GuiInputHandler's construction (the same post-construction back-wire
    // shape as `prompt` and `abandon_render_view`). Bound to
    // GuiInputHandler::cancel_archival_session, the body behind Esc's
    // render-cancel semantics. load_file calls it early; on the sole startup
    // load no archival session can exist yet, so the call no-ops.
    std::function<void()> cancel_archival_render;

    bool load_file(const std::string& path);
    void join_source_sample_cache_writer();

    // The writer reads GuiAudio's sample buffer through a raw pointer.
    // Callers join before replacing the buffer in load_file, and the
    // destructor joins before GuiAudio is destroyed.
    std::thread source_sample_cache_writer_;
};
