#pragma once

#include "active_views.h"
#include "app_state.h"
#include "audio.h"
#include "notifications.h"
#include "palette_file.h"   // GuiPaletteWords: the Waveform row's install
#include "playback_lifecycle.h"
#include "viewport.h"

#include <string>

struct Undo;
struct GuiTargetRender;
struct GuiInputHandler;

// THE SETTINGS EDITOR — THE ONE ROAD TO EVERY SETTING (architect 2026-10-21
// ~21:05, with the menu row's removal: "settings could gain a tab autocomplete
// for the keys, making the dropdown less useful" — "yes"). The band's Settings
// button and its chord, bare `;`, open it EMPTY — an empty line, the caret at
// column 0 — and the line is `key=value`, typed by hand. It is the flag
// editor's primitive (text_editor::State, kind-dispatched keyboard
// vocabulary, the whole text selected and a card on a refusal) painted IN THE
// MODAL FAMILY'S BOX (architect 2026-10-14, modal_box.h: the box is the edit
// control, the whole line on its field ground from the box's first row,
// soft-wrapped at its width and never scrolling, OK and Cancel flush right on
// the controls row under it, NO LABEL — "Setting:" removed, "pointless that
// far away"), the on-screen keyboard standing inside the frame under it on
// the glass as under every dialog editor. OK or Enter commits the line, Esc
// or Cancel closes it. (THE SETTINGS POPUP — a pull-down hanging from the
// Settings button, one row per key opening this editor PREFILLED with
// `<key>=<value>`, and the "True Colors" command row — stood 2026-07-31 to
// 2026-10-21 and is retired whole: its rows' keys are kSettingsEditorKeys
// below, and True Colors is the `true_colors` device key.)
//
// TAB COMPLETES, IN THREE STAGES, under the ONE AUTOCOMPLETE MODEL
// (route_modal_editor_key, input_key_dispatch.cpp: a Tab that advanced the
// buffer is consumed, one that did not walks the dialog's focus ring onto
// OK and Cancel — so a second Tab, or a Tab on a prefix that completes to
// nothing, is the walk; Tab shows no list, the popups being gone — THE ONE
// LIST is the PRESET PICKER since 2026-10-25, the `waveform` key's presets by
// name in the folder overlay, opened by the Settings button's shift twin
// (Shift+;, the glass long press), never by Tab, its pick committing through
// this editor's own apply, apply_waveform_preset):
//   1. THE KEY AND ITS `=` WHEN THE LINE NAMES ONE KEY. On a line with no
//      `=` yet, a COMPLETE key — any key this editor recalls, the ten,
//      the engine's and the per-tab band keys alike — and a PREFIX that ONE
//      key in kSettingsEditorKeys starts with both complete, in ONE Tab, to
//      the key and `=`, AND STOP THERE, the caret at the end (architect
//      2026-10-21 ~23:40, "no, the unambiguous should stop at the =":
//      `wave` to `waveform=`; the key, `=` and value in one Tab lived
//      one commit that evening, e7fc8d10, and from ~21:05 a prefix took the
//      key alone). A prefix several keys start with stops at their longest
//      common prefix (the path completer's rule; `projects_` against
//      `projects_repo` and `projects_path`); no match, an empty line or a prefix
//      already at the common prefix advances nothing.
//   2. THE KEY'S CURRENT VALUE. On `key=` with nothing after the `=` — the
//      first Tab's stop — the next Tab fills THE VALUE THE KEY HOLDS (the
//      2026-08-13 recall, unchanged), so a value is edited and never
//      retyped. The value is what the writer writes, byte for byte
//      (format_engine_setting_value, recall_gui_setting_value).
//   3. A CLOSED VALUE'S WORDS. On `key=<prefix>` where the key's domain is a
//      short closed list — `waveform`'s built-in preset names (2026-10-14) and
//      `true_colors`'s `on` / `off` (2026-10-21) — the prefix completes
//      against those words by the same rule as the keys
//      (complete_closed_value); `projects_path=<prefix>` completes against the
//      filesystem (complete_path_value, 2026-09-02). Every other non-empty
//      value side advances nothing.
// Every advance lands at the LINE'S END through text_editor::replace_selection,
// the one incoming filter.
//
// The editor is a keyboard front-end to EVERY key the product persists that a
// user edits in-app: every key that can appear in a `.settings` file, plus the
// FIVE editable ones the per-device config carries — gui_scale and
// projects_repo, which left the sidecar 2026-08-27 and kept this surface,
// since 2026-09-02 (architect) projects_path,
// which had been hand-edited only, and since 2026-10-14 (architect) the
// color key waveform (`chrome` until 2026-10-21; the caption key, editable
// 2026-10-14..10-21, left with its key), the color picker's until that day,
// and since 2026-10-21 true_colors (the popup's command row made a key) (the
// LED glow's tuning key, editable 2026-10-23..10-26, left with its key,
// the glow settled at a constant, materials.cpp's kLedGlowPx; the
// strip pad's key, editable 2026-10-22..10-23, left with it, the pad
// settled at a constant, program_spec.h's strip_pad_px; the
// two bevel keys, editable 2026-10-21..10-23, left with theirs; text_shadow, editable
// 2026-10-21..10-22, left with its key, the shadow one scaled px; icons
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
//    waveform and true_colors — in ONE body,
//    commit_device_setting: the key's own grammar
//    owner in device_config.h (palette_file.h for the waveform key)
//    decides (a refused commit and its card), the
//    live struct takes the value, and the commit WRITES THE DEVICE CONFIG —
//    that write is the whole persist, Ctrl+S carrying only the sidecar keys.
//    projects_repo is in force at once (every reader reads the
//    live field); projects_path is in force for the next Open project and the
//    next launch, the open project staying open, and the commit says so on a
//    card; waveform and true_colors at once, applied live
//    at the OK, no card (the screen is the answer).
//    gui_scale, the
//    one other
//    editable device key, stays in the GUI-kind router
//    below because it HAS a chokepoint (apply_gui_scale) and the router's job
//    is to reach one.
// 2. GUI-kind keys (viewport / zoom / playhead / active_audio_view /
//    active_markers_view / active_tab_view / per-tab trim /
//    per-tab read_only / gui_scale, and since 2026-10-26 the score's five —
//    score_path, which reads its folder at the commit, the range pair, which
//    takes the authoring lock, and the per-tab score_position, under their
//    own grammar owner and WRITTEN THROUGH to the score viewer's file at the
//    commit, score_viewer_file.h):
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
//    refuses no lock; the account is at GuiSettingsEditor::open. (Its road
//    onto a locked tab is bare `;` and the Settings button since 2026-10-21,
//    both on the read-only allowlist the iteration gate inherits — the
//    Settings popup's rows were that road until they retired the same day —
//    so the commit arms' lock refusals are where a locked key refuses.)
//
// NO CHOICE EDITOR (architect 2026-10-14, ~12:25: "we can remove dropdowns
// for chrome/caption and leave the autocomplete as the only help. dropdowns
// would need more work to make them real win95 style dropdowns and it's just
// not worth it"). THE CHOICE EDITOR — a Settings row whose key's domain was a
// short closed list opening this editor with a combo box where the field
// stood and its list dropping over the well — stood 2026-10-07 to 2026-10-14
// and is removed whole; a closed domain's help is Tab (stage 3 above), and
// the commit is OK's and Enter's — the screen changing at the commit, never
// before. (THE `waveform` KEY ALONE HAS A LIST AGAIN since 2026-10-25, the
// architect reversing this for it: "we rejected drop downs because it was
// too much work to make them Windows identical, but now we don't have the
// responsibility". It is no choice editor and no drop-down: the preset
// picker, the folder overlay's third content, whose row tap commits at once
// — apply_waveform_preset, this editor's own road.)
//
// THE KEYS TAB COMPLETES — ONE TABLE, THE TEN (the cues' four tuning keys
// gone 2026-10-26 ~13:05 and the LED glow's that afternoon, settled;
// `score_path` joined the same day): the five PIECE keys a hand edits (the
// piece's metadata, the engine keys of engine_settings.h, and the score
// folder, the score viewer's file's, typed only for another movement's
// folder, architect 2026-10-26), then the DEVICE
// CONFIG's five editable keys in that file's
// writer order (kDeviceConfigKeys, device_config.cpp — `last_project` and
// `role` have no editor). The other keys this editor commits (`scale`, `bpm`,
// the view and per-tab band keys, the score's range and positions, which
// the score viewer writes) are typed whole, Tab recalling their
// values once complete; a key added to the product's editable set joins this
// table by hand.
inline constexpr const char* kSettingsEditorKeys[] = {
    "title", "notes", "url", "cover", "score_path",
    "gui_scale", "projects_repo", "projects_path", "waveform", "true_colors",
};

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
    // keys). (The Settings dropdown's item clicks were the road onto a
    // locked tab's editor until the popup retired, 2026-10-21; bare `;` and
    // the Settings button are since, admitted by the lock.)
    GuiNotifications&     notifications;
    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds this editor by reference, so the
    // dependency is a cycle resolved with a pointer set post-construction —
    // the same shape as the viewport request_* callbacks). Used to reach the
    // gesture chokepoints that live on GuiInputHandler:
    // handle_active_audio_view_toggle, apply_gui_scale, apply_true_colors,
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

    // THE ONE OPEN (the head): an empty line, the caret at column 0. The band's
    // Settings button and bare `;` reach it, the button by synthesizing the
    // chord. (open_prefilled, the Settings popup's row click seeding
    // `<key>=<value>`, retired with the popup, 2026-10-21; Tab's stages 1 and
    // 2 are its typed spelling.)
    void open();
    void exit_no_commit();
    void commit();
    // THE COMPLETION, run on BARE TAB with the focus in the field — the head's
    // three stages: with no `=` on the line, a complete key or a prefix
    // naming one key taking the key and its `=` (complete_key_name the
    // prefix's), an ambiguous prefix its common prefix; with `key=` and an
    // empty
    // value side, that key's current live value; with a non-empty value side,
    // the path key's filesystem (complete_path_value) or a closed domain's
    // words (complete_closed_value), and nothing for every other key. The
    // value recalled is byte-identical to what a Ctrl+S (or, for a device
    // key, the config's writer) would write, UTF-8 provenance text included
    // since the 2026-08-02 relaxation (the remaining edge is stated at the
    // definition). An unknown key recalls nothing.
    //
    // IT RETURNS WHETHER THE BUFFER CHANGED, which is the ONE AUTOCOMPLETE
    // MODEL's whole question (architect 2026-08-13: "we should use one model
    // for all autocompletes" — Tab offers the completion first and walks the
    // modal's focus ring when it did not advance; the rule is stated once at
    // route_modal_editor_key's Tab arm, input_key_dispatch.cpp, and the typed
    // `=` this completion rode for part of that day is reverted with it). Every
    // no-op answers false, so each is a Tab that walks.
    bool autocomplete_value();

    // THE `waveform` KEY'S APPLY AND PERSIST — ONE ROAD, TWO DOORS (architect
    // 2026-10-25): the typed Waveform row's commit (commit_device_setting's
    // waveform arm, after its grammar and unchanged gates) and the PRESET
    // PICKER's pick (GuiInputHandler::preset_picker_commit) both land here.
    // The live key takes `name`, the preset is installed and the screen
    // repainted (install_waveform_preset), and the device config is written
    // (persist_device_config: a failure cards and the live colors stand for
    // the session). No card on success — the colors are the answer.
    // Precondition: is_builtin_palette_name(name), and `name` differs from
    // the live key (each door asks its own no-op first).
    void apply_waveform_preset(const std::string& name);

private:
    // THE DEVICE CONFIG'S WRITE, ADVISORY (the commit's persist): the whole
    // struct written (write_device_config, device_config.h); a failure prints
    // the diagnostic and cards the display, the live value standing either
    // way. Returns whether the file was written. Its readers are every device
    // key's commit (commit_device_setting's persist) and the waveform apply
    // above.
    bool persist_device_config();
    // THE DEVICE KEYS' COMMIT — `projects_repo=`, `projects_path=`,
    // `waveform=` and `true_colors=` — in one
    // body (the head's
    // item 1). Returns true when `key` is one of them, the commit then fully
    // handled inside (applied + deactivated, no-op-deactivated, or
    // refused with a card); false otherwise, so commit() goes on to the
    // routers.
    bool commit_device_setting(const std::string& key,
                               const std::string& value);
    // THE WAVEFORM ROW'S INSTALL (architect 2026-10-14; the color picker's
    // install_live_words until that day, its shape kept): the ten
    // installed (install_program_palette, palette_file.h), then the apply
    // shape run once, as install_program_palette's declaration states it —
    // the plate kicked when its inks moved, else the flag cache refreshed,
    // and the client invalidated. (The caption's keys rode beside, through
    // install_chrome_pick, with the whole surface's damage when the chrome
    // moved, 2026-10-14..10-21.)
    void install_waveform_preset(const GuiPaletteWords& words);
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
    // THE KEY COMPLETER (architect 2026-10-21, the popup's retirement), the
    // head's stage 1: `prefix` — the line less its leading whitespace, no `=`
    // on it — against kSettingsEditorKeys by byte compare (the keys are
    // ASCII), the buffer advancing at the line's end: a UNIQUE match
    // completes the key AND APPENDS ITS `=`, stopping there (architect
    // 2026-10-21 ~23:40; its value the next Tab's), an ambiguous prefix
    // stops at the matches' longest common prefix (`projects_` against `projects_repo` and `projects_path`), and no
    // advance — no match, an empty line, a prefix already at the common
    // prefix — is a Tab that walks. Returns whether the buffer advanced.
    bool complete_key_name(const std::string& prefix);
    // THE CLOSED-VALUE COMPLETER (architect 2026-10-14 afternoon, CONFIRMED:
    // "ok to add autocomplete", "agree all" — the preset completer for the
    // color key, `waveform` (`chrome` until 2026-10-21; the `caption` key's
    // arm, over its 43 schemes, 2026-10-14..10-21); `true_colors`'s two words
    // since 2026-10-21), the head's stage 3: the path completer's model on a
    // closed list. The value side is a PREFIX (byte compare, the words being
    // ASCII) of the key's words — the twenty built-in preset keys
    // (kGuiBuiltinPalettes' keys, palette_file.h) for `waveform`, `on` and
    // `off` (kTrueColorsWords, device_config.h) for `true_colors` — and the
    // buffer advances by the matches' longest common prefix past it: a
    // UNIQUE match completes the word WITH NO TRAILING CHARACTER (a name is
    // not a folder), an ambiguous prefix stops at the common prefix (he
    // accepts that a prefix completer cannot show the list), and no advance
    // — no match, a prefix already at the common prefix, a key with no
    // closed domain — is a Tab that walks. The empty value side keeps
    // recalling the live value (stage 2). The commit's grammar and its live
    // apply (commit_device_setting) are unchanged. Returns whether the buffer
    // advanced; `value` is the raw value side less its leading whitespace
    // (ltrim_ws's reasons).
    bool complete_closed_value(const std::string& key,
                               const std::string& value);
    // GUI-kind key router. Returns true when `key` is a recognized GUI-kind
    // key, in which case the commit is fully handled inside (applied +
    // deactivated, or refused with a card); false when `key` is not a
    // GUI-kind key, so
    // commit() falls through to the engine-key path. Each apply routes through
    // the key's own gesture chokepoint — no parallel state writer.
    bool commit_gui_setting(const std::string& key, const std::string& value);
};
