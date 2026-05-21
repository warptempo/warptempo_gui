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

    // Reseek the active playback session to a new starting sample, keeping
    // audio alive. The sample is expressed in the active playhead domain
    // (source-domain in source view; target-domain in target view; render-
    // domain in render-view). Handles the target-view target_buffer
    // translation internally. Caller is responsible for the entry-state
    // check (was_playing AND sample != playhead_at_entry); this function
    // unconditionally reseeks when called. For target view, samples
    // outside the target buffer's range fall back to playback.stop() —
    // keep-alive intent is well-defined for in-range positions only.
    void reseek_keeping_alive(int64_t sample);
};
