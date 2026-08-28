#pragma once

#include "active_views.h"
#include "app_state.h"
#include "audio.h"
#include "playback_lifecycle.h"
#include "viewport.h"

#include <string>

struct Undo;
struct GuiTargetRender;
struct GuiInputHandler;

// Settings-prompt editor cluster. Opens on `;`, accepts a single
// `key=value` line, applies it on Enter, and closes. Same primitive as
// the flag editor (text_editor::State, kind-dispatched keyboard
// vocabulary, red on parse failure) but painted as a MODAL ON THE BOTTOM ROW
// (2026-08-13 — its prefix the label at the row's left pad, the buffer in the
// dark inset field whose recolor is the red flash, OK and Cancel right-
// aligned) instead of over the flag rect.
//
// The editor is a keyboard front-end to EVERY key the product persists that a
// user edits in-app: every key that can appear in a `.settings` file, plus the
// three editable ones the per-device config carries — gui_scale, audio_player
// and projects_repo, which left the sidecar 2026-08-27 and kept this surface
// (the config's other two, projects_path and last_project, are the file's and
// the program's own and have no editor). It funnels each key into the SAME
// code its gesture uses (no parallel writers). commit() routes the typed key
// through:
// 1. audio_player (a launcher path with no gesture) and projects_repo (the
//    projects-home repository name, likewise gesture-less): set directly.
//    BOTH sets WRITE THE DEVICE CONFIG — that commit is each one's whole
//    persist, Ctrl+S carrying only the sidecar keys.
// 2. GUI-kind keys (viewport / zoom / playhead / follow / active_audio_view /
//    active_markers_view / active_tab_view / per-tab trim /
//    per-tab read_only / gui_scale / waveform_magnification_level):
//    commit_gui_setting
//    parses strictly (red-flash on any malformed or out-of-vocabulary value,
//    mirroring the load schema) then applies through the key's own gesture
//    chokepoint. These are launch/view state: no undo entry, no dirty; a
//    same-value commit no-op-deactivates like the engine no-op gate.
// 3. Canonical engine keys go through validate_engine_setting; on success
//    the typed field of app.engine_settings is updated and a settings-undo
//    entry pushed. Non-engine, non-canonical keys are rejected ("unknown
//    engine key") with a red-flash and no commit.
struct GuiSettingsEditor {
    AppState&             app;
    GuiAudio&             audio;
    Viewport&             viewport;
    // The engine-key commit rebuilds the warp map, so it clears any resting
    // region AND the selection with it (architect 2026-07-29 — the
    // teardown is at that commit's tail). Also the `tab_X_playhead=` commit's
    // deselect. This member exists for those clears; it acquired no other use when
    // the never-span-less collapse it once served was deleted.
    Selection&            selection;
    GuiActiveViews&       active_views;
    Undo&                 undo;
    GuiTargetRender&   target_render;
    GuiPlaybackLifecycle& playback_lifecycle;
    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds this editor by reference, so the
    // dependency is a cycle resolved with a pointer set post-construction —
    // the same shape as the viewport request_* callbacks). Used to reach the
    // gesture chokepoints that live on GuiInputHandler:
    // handle_active_audio_view_toggle, apply_gui_scale,
    // apply_waveform_magnification_level, commit_trim_mutation.
    GuiInputHandler*      input = nullptr;

    GuiSettingsEditor(AppState&             app_,
                      GuiAudio&             audio_,
                      Viewport&             viewport_,
                      Selection&            selection_,
                      GuiActiveViews&       active_views_,
                      Undo&                 undo_,
                      GuiTargetRender&   target_render_,
                      GuiPlaybackLifecycle& playback_lifecycle_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          active_views(active_views_),
          undo(undo_),
          target_render(target_render_),
          playback_lifecycle(playback_lifecycle_) {}

    void open();
    // Open the settings prompt PREFILLED with `<key>=<current value>`, the
    // settings dropdown's item click. Identical to open() plus the same recall
    // autocomplete_value performs, so the value is byte-identical to what a
    // Ctrl+S would write and there is no second serializer. The cursor rests at
    // the end of the line, ready to be edited or replaced; an unknown or
    // unrecallable key opens with the bare `<key>=` so the surface still tells
    // the user which key they picked.
    void open_prefilled(const char* key);
    void exit_no_commit();
    void commit();
    // The value completion, run on BARE TAB in the field and by open_prefilled:
    // when any settable key is typed with an empty
    // value side (e.g. `notes=`, `gui_scale=`, `tab_a_trim_begin=`),
    // replace the value side with that key's current live value for recall and
    // editing — byte-identical to what a Ctrl+S would write, UTF-8 provenance
    // text included since the 2026-08-02 relaxation (the old non-ASCII
    // exception is closed; the remaining edge is stated at the definition).
    // No-op when the value side is already non-empty, when there is no `=`, or
    // when the key is unknown.
    //
    // IT RETURNS WHETHER THE BUFFER CHANGED, which is the ONE AUTOCOMPLETE
    // MODEL's whole question (architect 2026-08-13: "we should use one model
    // for all autocompletes" — Tab offers the completion first and walks the
    // modal's focus ring when it did not advance; the rule is stated once at
    // route_modal_editor_key's Tab arm, input_key_dispatch.cpp, and the typed
    // `=` this completion rode for part of that day is reverted with it). Every
    // no-op above answers false, so each is a Tab that walks. open_prefilled
    // ignores the answer: it seeds the line either way.
    bool autocomplete_value();

private:
    // GUI-kind key router. Returns true when `key` is a recognized GUI-kind
    // key, in which case the commit is fully handled inside (applied +
    // deactivated, or red-flashed); false when `key` is not a GUI-kind key, so
    // commit() falls through to the engine-key path. Each apply routes through
    // the key's own gesture chokepoint — no parallel state writer.
    bool commit_gui_setting(const std::string& key, const std::string& value);
};
