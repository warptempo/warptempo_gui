#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "platform_wayland.h"
#include "viewport.h"

// X.7.11: playback-orchestration operations, extracted from
// main.cpp's inline lambdas. Owns the four GUI-level wrappers
// around GuiPlayback's mechanism: stop on gesture, restore the
// visible playhead at end-of-play, toggle play/stop, and apply a
// new speed. AppState, Viewport, GuiPlatform, and GuiAudio are
// captured directly. GuiPlayback stays a pure mechanism class —
// these operations live one layer up.
struct GuiPlaybackLifecycle {
    AppState&         app;
    const GuiAudio&   audio;
    GuiPlatform&      gui;
    GuiPlayback&      playback;
    Viewport&         viewport;

    GuiPlaybackLifecycle(AppState&         app_,
                         const GuiAudio&   audio_,
                         GuiPlatform&      gui_,
                         GuiPlayback&      playback_,
                         Viewport&         viewport_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_),
          viewport(viewport_) {}

    void stop_playback_if_playing();
    void restore_playhead_to_lsp();
    void toggle_playback();
    void set_playback_speed(float s);
};
