#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "viewport.h"

struct GuiTargetRender;

// X.7.7: mode/tab management cluster, extracted from main.cpp's inline
// lambdas and the inline Ctrl+Tab block in the keyboard handler. Owns the
// active-tab snapshot push (refresh_active_tab_from_app), the per-mode
// selection-slot resolver (active_view_state), the W/P view swap
// (switch_active_markers_view_to), the `p`-keypress entry path with engine
// gating (toggle_active_markers_view), and the Ctrl+Tab flip (switch_active_tab_to).
// clear_hover_popup is reached through viewport;
// stop_playback_if_playing through playback_lifecycle.
struct GuiTabMode {
    AppState&             app;
    const GuiAudio&       audio;
    Viewport&             viewport;
    Selection&            selection;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiTargetRender&   target_render;

    GuiTabMode(AppState&             app_,
               const GuiAudio&       audio_,
               Viewport&             viewport_,
               Selection&            selection_,
               GuiPlaybackLifecycle& playback_lifecycle_,
               GuiTargetRender&   target_render_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          playback_lifecycle(playback_lifecycle_),
          target_render(target_render_) {}

    void       refresh_active_tab_from_app();
    ViewState* active_view_state();
    void       switch_active_markers_view_to(char target_mode);
    void       switch_active_tab_to(char target_tab);
    void       toggle_active_markers_view();
};
