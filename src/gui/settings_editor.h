#pragma once

#include "app_state.h"
#include "audio.h"
#include "tab_mode.h"
#include "viewport.h"

struct Undo;
struct GuiTargetRender;

// Settings-prompt editor cluster. Opens on `;`, accepts a single
// `key=value` line, applies it on Enter, and closes. Same primitive as
// the V.A1 flag editor (text_editor::State, kind-dispatched keyboard
// vocabulary, red on parse failure) but painted in the bottom strip
// instead of over the flag rect.
//
// commit() routes the typed key through three paths:
// 1. View-state keys (viewport / zoom / playhead / follow / active_mode /
//    playback_speed, plus the per-tab trim keys) are rejected — they have
//    dedicated gestures.
// 2. trim_begin / trim_end parse the value as MM:SS.mmm and write through
//    to the active tab's ViewState; push a settings-undo entry.
// 3. Canonical engine keys go through validate_engine_setting; on success
//    the typed field of app.engine_settings is updated and a settings-undo
//    entry pushed. Non-engine, non-canonical keys are rejected ("unknown
//    engine key") with a red-flash and no commit.
struct GuiSettingsEditor {
    AppState&             app;
    GuiAudio&             audio;
    Viewport&             viewport;
    GuiTabMode&           tab_mode;
    Undo&                 undo;
    GuiTargetRender&   target_render;

    GuiSettingsEditor(AppState&             app_,
                      GuiAudio&             audio_,
                      Viewport&             viewport_,
                      GuiTabMode&           tab_mode_,
                      Undo&                 undo_,
                      GuiTargetRender&   target_render_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          tab_mode(tab_mode_),
          undo(undo_),
          target_render(target_render_) {}

    void open();
    void exit_no_commit();
    void commit();
};
