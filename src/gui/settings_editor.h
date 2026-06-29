#pragma once

#include "active_views.h"
#include "app_state.h"
#include "audio.h"
#include "viewport.h"

struct Undo;
struct GuiTargetRender;

// Settings-prompt editor cluster. Opens on `;`, accepts a single
// `key=value` line, applies it on Enter, and closes. Same primitive as
// the flag editor (text_editor::State, kind-dispatched keyboard
// vocabulary, red on parse failure) but painted in the bottom strip
// instead of over the flag rect.
//
// commit() routes the typed key through two paths:
// 1. View-state keys (viewport / zoom / playhead / follow / active_markers_view /
//    playback_speed / per-tab trim) are rejected — they have dedicated gestures.
//    Trim is gesture-owned, not typed in the settings editor.
// 2. Canonical engine keys go through validate_engine_setting; on success
//    the typed field of app.engine_settings is updated and a settings-undo
//    entry pushed. Non-engine, non-canonical keys are rejected ("unknown
//    engine key") with a red-flash and no commit.
struct GuiSettingsEditor {
    AppState&             app;
    GuiAudio&             audio;
    Viewport&             viewport;
    GuiActiveViews&       active_views;
    Undo&                 undo;
    GuiTargetRender&   target_render;

    GuiSettingsEditor(AppState&             app_,
                      GuiAudio&             audio_,
                      Viewport&             viewport_,
                      GuiActiveViews&       active_views_,
                      Undo&                 undo_,
                      GuiTargetRender&   target_render_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          active_views(active_views_),
          undo(undo_),
          target_render(target_render_) {}

    void open();
    void exit_no_commit();
    void commit();
    // Tab handler for the prompt: when an engine key is typed with an empty
    // value side (e.g. `notes=`), replace the value side with that key's
    // current stored value for recall and editing. No-op when the value
    // side is already non-empty, when there is no `=`, or when the key is
    // not a canonical engine key.
    void autocomplete_value();
};
