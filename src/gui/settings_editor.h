#pragma once

#include "app_state.h"
#include "audio.h"
#include "tab_mode.h"
#include "viewport.h"

// Settings-prompt editor cluster. Opens on `;`, accepts a single
// `key=value` line, applies it to settings_passthrough on Enter, and
// closes. Same primitive as the V.A1 flag editor (text_editor::State,
// kind-dispatched keyboard vocabulary, red on parse failure) but painted
// in the bottom strip instead of over the flag rect.
//
// engine= changes from P mode boot the active_mode back to W when the
// new engine value isn't `warptempo` — mirrors the entry gate in
// GuiTabMode::toggle_active_mode in the leaving direction.
struct GuiSettingsEditor {
    AppState&    app;
    GuiAudio&    audio;
    Viewport&    viewport;
    GuiTabMode&  tab_mode;

    GuiSettingsEditor(AppState&   app_,
                      GuiAudio&   audio_,
                      Viewport&   viewport_,
                      GuiTabMode& tab_mode_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          tab_mode(tab_mode_) {}

    void open();
    void exit_no_commit();
    void commit();
};
