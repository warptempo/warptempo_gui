#pragma once

#include "active_views.h"
#include "app_state.h"
#include "audio.h"
#include "notifications.h"
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
// FOUR editable ones the per-device config carries — gui_scale and
// projects_repo, which left the sidecar 2026-08-27 and kept this surface, and
// since 2026-09-02 (architect, the four-tier review's R-22) projects_path and
// sync_path, which had been hand-edited only (the config's fifth key,
// last_project, is the program's own and has no editor; `audio_player`, once
// the third editable device key, retired whole 2026-08-28 with the in-app
// render player). It funnels each key into the SAME code its gesture uses (no
// parallel writers). commit() routes the typed key through:
// 1. The three gesture-less device keys — projects_repo, projects_path,
//    sync_path — in ONE body, commit_device_setting: the key's own grammar
//    owner in device_config.h decides (red flash and card on refusal), the
//    live struct takes the value, and the commit WRITES THE DEVICE CONFIG —
//    that write is the whole persist, Ctrl+S carrying only the sidecar keys.
//    projects_repo and sync_path are in force at once (every reader reads the
//    live field); projects_path is in force for the next Open project and the
//    next launch, the open project staying open, and the commit says so on a
//    card. gui_scale, the fourth device key, stays in the GUI-kind router
//    below because it HAS a chokepoint (apply_gui_scale) and the router's job
//    is to reach one.
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
//    engine key") with a red-flash and no commit. This arm is also where the
//    read-only lock lives since 2026-09-04 (architect: the lock governs the
//    keys, not the surface): an engine key IS the piece, so a locked active
//    tab refuses it with the lock's own card, while items 1 and 2 above —
//    the device keys and the band — commit on a locked tab. The opener
//    refuses nothing; the account is at GuiSettingsEditor::open.
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
    // The card surface, wired 2026-08-30 for the refusals this unit answers
    // with a sentence: every red flash says its reason on a card, and the
    // read-only lock says its own at the engine-key commit arm (it stood at
    // open() from 2026-08-30 until 2026-09-04, when the lock moved to the
    // keys). The SETTINGS DROPDOWN's item clicks are the only road onto a
    // locked tab's editor — bare `;` is off the read-only allowlist and dies
    // at the keyboard gate, whose own card says the same words — and the
    // dropdown items never grey by ruling, so their commands' refusals are
    // what answer them.
    GuiNotifications&     notifications;
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
                      GuiPlaybackLifecycle& playback_lifecycle_,
                      GuiNotifications&     notifications_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          active_views(active_views_),
          undo(undo_),
          target_render(target_render_),
          playback_lifecycle(playback_lifecycle_),
          notifications(notifications_) {}

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
    // when the key is unknown — EXCEPT on the two path keys (2026-09-02), where
    // a NON-EMPTY value side is a path prefix and Tab completes it against the
    // filesystem through complete_path_value below.
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
    // THE THREE GESTURE-LESS DEVICE KEYS' COMMIT — `projects_repo=`,
    // `projects_path=`, `sync_path=` — in one body (the head's item 1).
    // Returns true when `key` is one of the three, the commit then fully
    // handled inside (applied + deactivated, no-op-deactivated, or
    // red-flashed); false otherwise, so commit() goes on to the routers.
    bool commit_device_setting(const std::string& key,
                               const std::string& value);
    // THE PATH COMPLETER (2026-09-02), autocomplete_value's arm for the two
    // path keys — `projects_path` and `sync_path` — when the value side is
    // NON-EMPTY: the value side is split at its last `/`, the directory the
    // head names is listed afresh (absolute only, the grammar's own rule; a
    // relative head completes nothing, and so does a listing error), the
    // DIRECTORIES whose names start with the tail (byte compare — both keys
    // name folders) are the matches, and the buffer advances by their longest
    // common prefix past the tail, a single match taking a trailing `/`.
    // Nothing is cached. The append rides text_editor::replace_selection — the
    // one incoming filter — so the bytes are UTF-8-transparent exactly as a
    // paste is. Returns whether the buffer advanced, the one autocomplete
    // model's question; `value` is the raw value side (untrimmed: the
    // completion works on what is literally typed).
    bool complete_path_value(const std::string& value);
    // GUI-kind key router. Returns true when `key` is a recognized GUI-kind
    // key, in which case the commit is fully handled inside (applied +
    // deactivated, or red-flashed); false when `key` is not a GUI-kind key, so
    // commit() falls through to the engine-key path. Each apply routes through
    // the key's own gesture chokepoint — no parallel state writer.
    bool commit_gui_setting(const std::string& key, const std::string& value);
};
