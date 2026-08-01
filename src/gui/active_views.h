#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "viewport.h"

// Active-views management cluster, extracted from main.cpp's inline
// lambdas and the inline Ctrl+Tab block in the keyboard handler. Owns the
// two view-axis swap operations (W/P markers and A/B tab) plus their
// shared snapshot machinery: active-tab snapshot push
// (refresh_active_tab_view_from_app), the W/P markers-view swap
// (switch_active_markers_view_to), the `p`-keypress entry path with
// engine gating (toggle_active_markers_view), and the Ctrl+Tab tab-view
// flip (switch_active_tab_view_to). The S/T audio-view axis is handled
// elsewhere (input_handler) — it's a domain translation, not a snapshot
// swap, and lives outside this cluster's scope by design.
// Damage and viewport mutation are reached through viewport;
// stop_playback_if_playing through playback_lifecycle.
struct GuiActiveViews {
    AppState&             app;
    const GuiAudio&       audio;
    Viewport&             viewport;
    Selection&            selection;
    GuiPlaybackLifecycle& playback_lifecycle;

    GuiActiveViews(AppState&             app_,
                   const GuiAudio&       audio_,
                   Viewport&             viewport_,
                   Selection&            selection_,
                   GuiPlaybackLifecycle& playback_lifecycle_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          playback_lifecycle(playback_lifecycle_) {}

    void       refresh_active_tab_view_from_app();
    void       switch_active_markers_view_to(char target_mode);
    void       switch_active_tab_view_to(char target_tab);
    void       toggle_active_markers_view();
};
