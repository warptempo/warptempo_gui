#pragma once

#include "active_views.h"
#include "app_state.h"
#include "audio.h"
#include "notifications.h"
#include "palette_file.h"   // GuiPaletteWords, GuiChromePick: the color rows' install
#include "playback_lifecycle.h"
#include "viewport.h"

#include <optional>
#include <string>

struct Undo;
struct GuiTargetRender;
struct GuiInputHandler;

// Settings-prompt editor cluster. Opens on `;`, accepts a single
// `key=value` line, applies it on Enter, and closes. Same primitive as
// the flag editor (text_editor::State, kind-dispatched keyboard
// vocabulary, the whole text selected and a card on a refusal) but painted
// IN THE MODAL FAMILY'S BOX (architect 2026-10-14, modal_box.h: the box is
// the edit control, the whole `key=value` line on its field ground from the
// box's first row, soft-wrapped at its width and never scrolling, OK and
// Cancel flush right on the controls row under it, NO LABEL — "Setting:"
// removed, "pointless that far away"; a modal on the bottom row from
// 2026-08-13, its label, its sunken field and its two buttons in one row)
// instead of over the flag rect.
//
// The editor is a keyboard front-end to EVERY key the product persists that a
// user edits in-app: every key that can appear in a `.settings` file, plus the
// FIVE editable ones the per-device config carries — gui_scale and
// projects_repo, which left the sidecar 2026-08-27 and kept this surface,
// since 2026-09-02 (architect) projects_path,
// which had been hand-edited only, and since 2026-10-14 (architect) the two
// color keys chrome and caption, the color picker's until that day (icons
// and font, editable 2026-10-09..10-12, the vocabulary chrome,
// editable 2026-10-07..10-10, and theme,
// editable 2026-10-03..10-08, its level and the twelve
// program colour keys, editable 2026-10-03..04, left with their keys; the
// config's key last_project is the program's own and has no editor; the
// waveform's height cap, editable 2026-09-13..10-07, left with its key, the
// waveform being the lanes' leftover; the hold delay's
// key, editable for its
// one-day tuning phase 2026-09-29, is struck, the value hard-coded as
// kHoldDelayMs; `audio_player`, once
// the third editable device key, retired whole 2026-08-28 with the in-app
// render player). It funnels each key into the SAME code its gesture uses (no
// parallel writers). commit() routes the typed key through:
// 1. The device keys other than the scale — projects_repo, projects_path,
//    chrome and caption — in ONE body,
//    commit_device_setting: the key's own grammar
//    owner in device_config.h (palette_file.h for the two color keys)
//    decides (a refused commit and its card), the
//    live struct takes the value, and the commit WRITES THE DEVICE CONFIG —
//    that write is the whole persist, Ctrl+S carrying only the sidecar keys.
//    projects_repo is in force at once (every reader reads the
//    live field); projects_path is in force for the next Open project and the
//    next launch, the open project staying open, and the commit says so on a
//    card; chrome and caption at once, the preset installed live at the OK,
//    no card (the screen is the answer).
//    gui_scale, the
//    one other
//    editable device key, stays in the GUI-kind router
//    below because it HAS a chokepoint (apply_gui_scale) and the router's job
//    is to reach one.
// 2. GUI-kind keys (viewport / zoom / playhead / active_audio_view /
//    active_markers_view / active_tab_view / per-tab trim /
//    per-tab read_only / gui_scale):
//    commit_gui_setting
//    parses strictly (a refused commit on any malformed or out-of-vocabulary value,
//    mirroring the load schema) then applies through the key's own gesture
//    chokepoint. These are launch/view state: no undo entry, no dirty; a
//    same-value commit no-op-deactivates like the engine no-op gate.
//    per-tab read_only was the one arm that wrote its band directly, and so
//    left the icon row's Read-Only glyph stale on a typed self-lock or self-unlock;
//    since 2026-09-04 it goes through set_tab_read_only (input_handler.h) with
//    bare `o`, and the promise above holds for every key in the list.
// 3. Canonical engine keys go through validate_engine_setting; on success
//    the typed field of app.engine_settings is updated and a settings-undo
//    entry pushed. Non-engine, non-canonical keys are rejected ("unknown
//    engine key") with a refused commit and no write. This arm is also where the
//    read-only lock lives since 2026-09-04 (architect: the lock governs the
//    keys, not the surface): an engine key IS the piece, so a locked active
//    tab refuses it with the lock's own card, while items 1 and 2 above —
//    the device keys and the band — commit on a locked tab. The opener
//    refuses no lock; the account is at GuiSettingsEditor::open.
//
// EVERY ROW IS THIS ONE LINE EDITOR (architect 2026-10-14, ~12:25: "we can
// remove dropdowns for chrome/caption and leave the autocomplete as the only
// help. dropdowns would need more work to make them real win95 style
// dropdowns and it's just not worth it"). THE CHOICE EDITOR — a Settings row
// whose key's domain was a short closed list opening this editor with a
// combo box where the field stood and its list dropping over the well, the
// shown value moved by the list and the arrows and committed at OK alone —
// stood 2026-10-07 to 2026-10-14 (the vocabulary Chrome row 2026-10-07..10,
// the Icons and Font rows 2026-10-09..12, the Chrome and Caption rows its
// last); it is removed whole with its combo, its list, the popup lists'
// scroll road it was the last reader of and the on-screen keyboard's term for
// it. THE CHROME AND CAPTION ROWS' HELP IS TAB (complete_preset_value,
// below): a typed prefix completes against the kind's built-in names, and
// the commit is OK's and Enter's as on every row — the screen changing at
// the commit, never before.
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
    // with a sentence: every refused commit says its reason on a card, and the
    // read-only lock says its own at the engine-key commit arm (it stood at
    // open() from 2026-08-30 until 2026-09-04, when the lock moved to the
    // keys). The SETTINGS DROPDOWN's item clicks are the only road onto a
    // locked tab's editor — bare `;` is off the read-only allowlist and dies
    // at the keyboard gate, whose own card says the same words — and the
    // dropdown items grey on a load alone (dropdown_item_enabled), the lock
    // being no refusal of the OPEN, so the commit's refusals are what answer
    // them.
    GuiNotifications&     notifications;
    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds this editor by reference, so the
    // dependency is a cycle resolved with a pointer set post-construction —
    // the same shape as the viewport request_* callbacks). Used to reach the
    // gesture chokepoints that live on GuiInputHandler:
    // handle_active_audio_view_toggle, apply_gui_scale,
    // commit_trim_mutation, set_tab_read_only.
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
    // autocomplete_value performs, so there is no second serializer: the
    // value is what the writer writes, byte for byte, the two color keys
    // (`chrome`, `caption`) included — required since 2026-10-14, so their
    // line is the preset on screen (recall_gui_setting_value). The cursor
    // rests at
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
    // when the key is unknown — EXCEPT on the path key (2026-09-02), where
    // a NON-EMPTY value side is a path prefix and Tab completes it against the
    // filesystem through complete_path_value below, and on THE TWO COLOR KEYS
    // (architect 2026-10-14), where a non-empty value side is a preset-name
    // prefix and Tab completes it against the kind's built-in names through
    // complete_preset_value below — the path key's model.
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
    // THE DEVICE KEYS' COMMIT — `projects_repo=`, `projects_path=`,
    // `chrome=` and `caption=` — in one body (the head's
    // item 1). Returns true when `key` is one of them, the commit then fully
    // handled inside (applied + deactivated, no-op-deactivated, or
    // refused with a card); false otherwise, so commit() goes on to the
    // routers.
    bool commit_device_setting(const std::string& key,
                               const std::string& value);
    // THE COLOR ROWS' INSTALL (architect 2026-10-14; the color picker's
    // install_live_words until that day, its shape kept): the eleven
    // installed (install_program_palette, palette_file.h) and the caption's
    // keys (install_chrome_pick, render.h), then the apply shape run once, as
    // install_program_palette's declaration states it — the plate kicked when
    // its inks moved, else the flag cache refreshed; ITS DAMAGE a changed
    // chrome's whole surface, the window's sizing frame with it
    // (Viewport::invalidate_surface), any other palette change the client
    // alone (invalidate_all) — the rule at the body.
    void install_color_preset(const GuiPaletteWords& words,
                              const std::optional<GuiChromePick>& scheme);
    // THE PATH COMPLETER (2026-09-02), autocomplete_value's arm for the path
    // key — `projects_path` — when the value side is
    // NON-EMPTY: the value side is split at its last `/`, the directory the
    // head names is listed afresh (absolute only, the grammar's own rule; a
    // relative head completes nothing, and so does a listing error), the
    // DIRECTORIES whose names start with the tail (byte compare — the key
    // names a folder) are the matches, and the buffer advances by their longest
    // common prefix past the tail, a single match taking a trailing `/`.
    // Nothing is cached. The append rides text_editor::replace_selection — the
    // one incoming filter — so the bytes are UTF-8-transparent exactly as a
    // paste is. Returns whether the buffer advanced, the one autocomplete
    // model's question; `value` is the raw value side (untrimmed: the
    // completion works on what is literally typed).
    bool complete_path_value(const std::string& value);
    // THE PRESET COMPLETER (architect 2026-10-14 afternoon, CONFIRMED: "ok to
    // add autocomplete", "agree all"), autocomplete_value's arm for the two
    // color keys — `chrome` and `caption` — when the value side is NON-EMPTY,
    // the path completer's model on a closed list: the value side is a
    // PREFIX (byte compare, the names being ASCII) of the kind's compiled-in
    // preset names — Cool Edit's twenty for `chrome` (kGuiBuiltinPalettes'
    // keys), the 42 Windows and Plus! schemes for `caption`
    // (kGuiChromeSchemes' keys), palette_file.h — and the buffer advances by
    // the matches' longest common prefix past it: a UNIQUE match completes
    // the name WITH NO TRAILING CHARACTER (a name is not a folder), an
    // ambiguous prefix stops at the common prefix (`caption=w` stops at
    // `windows-`; he accepts that a prefix completer cannot show the list),
    // and no advance — no match, or a prefix already at the common prefix —
    // is a Tab that walks. The empty value side keeps recalling the live
    // preset's key (the recall above). The commit's grammar and its live apply
    // (commit_device_setting, install_color_preset) are unchanged. Returns
    // whether the buffer advanced; `key` is the trimmed key, `value` the raw
    // value side less its leading whitespace (ltrim_ws's reasons).
    bool complete_preset_value(const std::string& key,
                               const std::string& value);
    // GUI-kind key router. Returns true when `key` is a recognized GUI-kind
    // key, in which case the commit is fully handled inside (applied +
    // deactivated, or refused with a card); false when `key` is not a
    // GUI-kind key, so
    // commit() falls through to the engine-key path. Each apply routes through
    // the key's own gesture chokepoint — no parallel state writer.
    bool commit_gui_setting(const std::string& key, const std::string& value);
};
