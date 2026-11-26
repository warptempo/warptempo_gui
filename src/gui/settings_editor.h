#pragma once

#include "app_state.h"
#include "audio.h"
#include "notifications.h"
#include "playback_lifecycle.h"
#include "viewport.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct Selection;
struct Undo;
struct GuiTargetRender;
struct GuiInputHandler;

// THE SETTINGS EDITOR — THE LINE EDITOR OF THE SETTINGS PAGE'S ROWS. IT
// OPENS FROM THE SETTINGS PAGE (architect 2026-10-28): the top icon row's
// Settings key and its chord, bare `;`, raise the page, the folder
// overlay's list of kSettingsEditorKeys below, one `key=value` row each,
// `scale` and `bpm` shown beside them as disabled rows
// (kSettingsPagePieceKeys, architect 2026-10-29)
// (GuiInputHandler::open_settings_page), and the tap of an
// editable row whose key has no value list closes the page and opens this
// editor ON THAT ROW'S KEY, ITS ENTRY PREFILLED WITH THE VALUE THE KEY HOLDS,
// the caret at its end. It is the flag editor's primitive (text_editor::State,
// kind-dispatched keyboard vocabulary, the whole text selected and a card on
// a refusal) painted AS THE VALUE SUB-PAGE (architect 2026-11-20:
// "a subpage where if the user clicks on a value a single line text input is
// shown ... on that page, the only controls are the osk's esc and enter";
// paint_handler.cpp's THE VALUE SUB-PAGE, the owner): the box's view one
// flat page, on it the value in Arc's focused entry across the view,
// NOTHING ELSE — no title, no label (architect 2026-11-22), no frame, no
// button, no list behind — the top icon row greyed whole, SETUP
// lit and inert (redesign_button_enabled's first arm,
// roster_face_page_owner, app_state.h), the on-screen
// keyboard over the view's foot, the bottom icon row and the status bar
// (onscreen_keyboard.h's placement block), ITS ENTER AND ITS ESC THE PAGE'S
// ONLY CONTROLS, the entry's text alone live beside them (a tap or a drag
// seats the caret).
// THE ENTRY IS ONE LINE AND A VALUE
// LONGER THAN IT SCROLLS SIDEWAYS, GTK'S WAY (architect 2026-11-19, never a
// wrapped line: "The pen does have a good way to
// scroll, and those are the left and right arrows of the keyboard. That's
// exactly why they're there."): the text cut at the entry's inner edges and
// scrolled just enough to keep the caret in view, the keyboard's LEFT /
// RIGHT and their hold-repeat the scroll's road, a tap seating the caret
// through the offset (text_editor::State::view_offset_px).
//
// THE ENTRY HOLDS THE VALUE ALONE (architect 2026-11-19, asked
// whether the key could no longer be retyped, the line's retyping being the
// only typed road to the keys the page lists no editable row for: "One,
// agree." — the BPM form's field's shape). The key is the
// open's (page_key) and nothing typed changes it; Enter COMPOSES the
// assignment from that key and the entry's value — its surrounding
// whitespace trimmed, a `=` or a space inside it kept (a url's `?v=`, a
// notes line) — and commits it through the key's own road below. So the
// keys with no editable row — a tab's read-only bit, the view keys, the
// per-tab viewport, zoom, playhead and trim, the score's range and
// positions, `scale` and `bpm` — have no road through this editor: each is
// written by its own lever (the padlock key, the view keys and the tab pair, the
// gestures, Reset Trim, the score viewer's START / END and its pages, the
// BPM form and the loads), the per-tab keys his "I won't set it manually"
// (2026-10-28). Esc closes it uncommitted, and EITHER WAY THE PAGE COMES BACK,
// seated on the row the editor was opened from (architect 2026-10-28 night:
// "otherwise it's assumed I am editing one key only" — the editor a sub-page
// of the page, as the value lists are;
// GuiInputHandler::handle_settings_editor_key, the one return hook); a
// refused commit leaves the editor standing, its whole text selected, and
// reopens nothing.
//
// TAB COMPLETES, IN TWO STAGES, on the open's key, under the ONE
// AUTOCOMPLETE MODEL (route_modal_editor_key, input_key_dispatch.cpp: a Tab
// that advanced the buffer and one that did not are both consumed — a
// second Tab, or a Tab that completes nothing, is silent, the sub-page
// having no button to walk to; Tab shows no list — THE LISTS
// are the settings page's keys and THE VALUE LISTS, a listed key's values in
// the folder overlay (kSettingsValueListKeys below), opened by the page's
// row for that key, never by Tab, a pick committing through this editor's
// own body, apply_device_setting):
//   1. THE KEY'S CURRENT VALUE. On an EMPTY entry (whitespace alone counts
//      as empty) Tab fills THE VALUE THE KEY HOLDS (the 2026-08-13 recall,
//      the open's own prefill), so a value cleared by hand comes back and is
//      edited, never retyped. The value is what the writer writes, byte for
//      byte (setting_value: format_engine_setting_value,
//      recall_gui_setting_value, recall_score_viewer_value).
//   2. THE PATH. On a non-empty entry, `projects_path`'s value is a path
//      prefix and completes against the filesystem (complete_path_value,
//      2026-09-02); every other key's non-empty entry advances nothing.
// Every advance lands through text_editor::replace_selection, the one
// incoming filter.
//
// The editor is a keyboard front-end to THE EDITABLE KEYS THE SETTINGS PAGE
// LISTS whose domain is no short list: the piece's `title`, `notes`, `url`
// and `cover` and the score's folder `score_path`, and the per-device
// config's `gui_scale` and every editable device key without a value list —
// projects_repo, projects_path and the noise's fixed attenuation
// noise_attenuation_level; the four
// keys with a value list — true_colors, waveform_grow, the noise's folder
// noise and its crossfade noise_crossfade_s — commit through this editor's
// own body from their lists. (The config's key last_project is the
// program's own and has no editor; the level noise_level and the level its
// tap restores, noise_restore_level, are the top icon row's NOISE case's,
// and lit_lamps is the sticky lamps' own keys', the main-page rule at
// kSettingsEditorKeys.) It
// funnels each key into the SAME code its other door uses (no parallel
// writers). commit() routes the open's key through:
// 1. The device keys other than the scale — projects_repo, projects_path,
//    true_colors, waveform_grow, noise, noise_attenuation_level and
//    noise_crossfade_s — in ONE body,
//    apply_device_setting (commit_device_setting the line's door onto it,
//    the value list the other, the four listed keys reaching it through the
//    list alone): the key's own grammar owner in device_config.h decides (a
//    refused commit and its card), the live struct takes the value, and the
//    commit WRITES THE DEVICE CONFIG — that write is the whole persist,
//    Ctrl+S carrying only the sidecar keys.
//    projects_repo is in force at once (every reader reads the
//    live field); projects_path is in force for the next Open project and the
//    next launch, the open project staying open, and the commit says so on a
//    card; true_colors at once, applied live at the list's tap, no card (the
//    screen is the answer); waveform_grow the same,
//    the window repainted (GuiInputHandler::apply_waveform_grow), no card
//    (the waveform is the answer); the noise's attenuation at once, handed
//    to the one GuiNoise (GuiNoise::set_attenuation_db: the gain
//    recomputed, the switch untouched) at the Enter, no card (the sound is
//    the answer), and its crossfade the same (GuiNoise::set_crossfade_s); the
//    noise's folder at once, the resident
//    recordings freed and the new folder loaded under an audible level
//    (GuiNoise::change_folder), no card (the NOISE case's face is the
//    answer).
// 2. `gui_scale` and `score_path` (commit_gui_setting): gui_scale, the one
//    other editable device key, stays out of the body because it HAS a
//    chokepoint of its own (apply_gui_scale, which writes the device config
//    itself); score_path is the score viewer's file's key, under that file's
//    grammar owner, its folder READ at the commit and WRITTEN THROUGH to the
//    file (score_viewer_file.h). Neither has an undo entry or a dirty bit; a
//    same-value commit no-op-deactivates like the engine no-op gate.
// 3. The piece's four editable engine keys, `title`, `notes`, `url` and
//    `cover`, go through validate_engine_setting; on success the field of
//    app.engine_settings is updated and a settings-undo entry pushed. This
//    arm is also where the read-only lock lives (architect 2026-09-04: the
//    lock governs the keys, not the surface): an engine key IS the
//    piece, so a locked active tab refuses it with the lock's own card,
//    while items 1 and 2 above commit on a locked tab. The opener refuses no
//    lock; the account is at GuiSettingsEditor::open. (Its road onto a
//    locked tab is the settings page's rows, the page opened by bare `;` and
//    the Settings button, both on the read-only allowlist the iteration gate
//    inherits, so the commit arm's lock refusal is where a locked key
//    refuses.)
//
// NO CHOICE EDITOR: no row opens this editor with a combo box. A key whose
// domain is a short closed list has a VALUE LIST instead (architect
// 2026-10-25: "we rejected drop downs because it was too much work to make
// them Windows identical, but now we don't have the responsibility";
// kSettingsValueListKeys below), no drop-down either: the folder overlay's
// content behind the settings page's row for its key, whose row tap commits
// at once — apply_device_setting, this editor's own road. THE SETTINGS PAGE
// IS NO CHOICE EDITOR EITHER (2026-10-28): its other rows commit nothing,
// each opening this line editor.
//
// THE EDITABLE KEYS — ONE TABLE, THE THIRTEEN (architect 2026-11-26): the
// five
// PIECE keys a hand edits (the
// piece's metadata, the engine keys of engine_settings.h, and the score
// folder, the score viewer's file's, typed only for another movement's
// folder, architect 2026-10-26), then the DEVICE
// CONFIG's eight editable keys in that file's
// writer order (kDeviceConfigKeys, device_config.cpp — `last_project` has
// no editor, nor have the level, the level its tap restores and the sticky
// lamps, below). THE TABLE IS THE SETTINGS PAGE'S
// EDITABLE ROSTER, EXACTLY, AND THE ONLY KEYS THIS EDITOR WRITES (architect
// 2026-10-28: the per-tab keys stay off, "I won't set it manually", and so
// does every key with a GUI lever — follow, read-only, the view keys —
// EXCEPT `scale` and `bpm`, which the BPM key writes: the page SHOWS those
// two as DISABLED rows, architect 2026-10-29, "bpm and scale should be
// viewable but disabled. they are useful for notes", and they stay out of
// this table, so the page's rows never offer them to the line editor;
// kSettingsPagePieceKeys below; architect 2026-11-19: the entry holds the
// value alone, so no key off this table has a typed road); a key added to the
// product's editable set joins this table by hand. A KEY SHOWN AND EDITED ON
// THE MAIN PAGE GETS NO SETTINGS ROW (architect 2026-11-05): the level,
// `noise_level`, is seen and set on the top icon row's NOISE case
// (RedesignButton::IconNoiseLevel, app_state.h), so it is not here — no
// row, no arm, no recall — and stays REQUIRED in the config,
// persisted by the case's lift and written by the config's writer with
// every key; so are, by the same rule (architect 2026-11-24), the level the
// case's tap restores, `noise_restore_level`, written by that tap alone,
// and the sticky lamps, `lit_lamps`, seen and set on their own four keys
// (MAGNIFY, FOLLOW, RESTR, TIPS) and written by the tick's comparator
// (main.cpp's refresh_lit_lamps).
// GuiInputHandler::build_settings_page_rows lists the device keys first in
// this table's order, the split at kSettingsEditorPieceKeyCount, and then
// the piece rows in kSettingsPagePieceKeys' order, no title row over either
// (architect 2026-11-22).
inline constexpr const char* kSettingsEditorKeys[] = {
    "title", "notes", "url", "cover", "score_path",
    "gui_scale", "projects_repo", "projects_path", "true_colors",
    "waveform_grow", "noise", "noise_attenuation_level", "noise_crossfade_s",
};
// THE PIECE KEYS' COUNT — the table's first five; the rest are the device
// config's. The page's two runs of rows split the table here, and the asserts pin
// the boundary to its two names so a key added on the wrong side fails the
// build.
inline constexpr std::size_t kSettingsEditorPieceKeyCount = 5;
static_assert(std::string_view(kSettingsEditorKeys[kSettingsEditorPieceKeyCount - 1]) ==
                  "score_path" &&
              std::string_view(kSettingsEditorKeys[kSettingsEditorPieceKeyCount]) ==
                  "gui_scale");

// IS `key` ONE OF kSettingsEditorKeys — a key the settings page lists as an
// editable row, and so one this editor or its value list writes?
inline constexpr bool is_settings_editor_key(std::string_view key) {
    for (const char* k : kSettingsEditorKeys)
        if (key == k) return true;
    return false;
}

// THE SETTINGS PAGE'S PIECE ROWS, IN THE SETTINGS FILE'S ORDER (kSettingsOrder,
// settings_io.cpp; architect 2026-10-29): the five piece keys above and,
// after `title`, the two the BPM key writes, `scale` and `bpm`, SHOWN
// DISABLED — "bpm and scale should be viewable but disabled. they are useful
// for notes". A key here that is not one of kSettingsEditorKeys is a
// DISABLED row (AppState::FolderOverlayRow::Kind::Disabled): its line as
// GuiSettingsEditor::setting_line spells it, in the dim print (`text_dim`), never
// seated and taking no press (folder_overlay::row_is_inert). The assert pins
// the table's editable keys to the editor table's piece keys, in order, so
// the page's piece rows and the editor's roster cannot drift apart.
inline constexpr const char* kSettingsPagePieceKeys[] = {
    "title", "scale", "bpm", "notes", "url", "cover", "score_path",
};
consteval bool settings_page_piece_keys_pinned() {
    std::size_t editable = 0;
    for (const char* k : kSettingsPagePieceKeys) {
        if (!is_settings_editor_key(k)) continue;
        if (editable >= kSettingsEditorPieceKeyCount ||
            std::string_view(k) != kSettingsEditorKeys[editable])
            return false;
        ++editable;
    }
    return editable == kSettingsEditorPieceKeyCount;
}
static_assert(settings_page_piece_keys_pinned());

// THE KEYS WITH A VALUE LIST — ONE TABLE, THE FOUR (architect 2026-10-28,
// 2026-10-31 for `waveform_grow`): the settings page's row for one of these swaps the folder overlay's content to THE
// KEY'S VALUES (GuiInputHandler::build_value_list_rows, its rows from
// GuiSettingsEditor::value_choices below), and a row's tap commits that value
// at once through apply_device_setting, the device keys' one body, then
// returns to the page on that key's row (the list a sub-page of the page,
// architect 2026-10-28). Every other page row opens the line editor. Each
// is a device key with a short domain the list can show whole: the two words, the
// folders under `noise/`, the crossfade's 30 whole seconds and the grow's
// 9 quarters. A LIST IS FLAT, its rows the values alone, no title row
// (architect 2026-11-22: "the user is expected to have expert
// knowledge"). THE LIST IS THE KEY'S ONE DOOR: its
// row never opens the line editor (settings_page_commit), so its value
// reaches apply_device_setting through the list's tap alone.
// (The noise's attenuation is NOT here: a decimal is no short domain, so
// its row opens the line editor.)
inline constexpr const char* kSettingsValueListKeys[] = {
    "true_colors", "waveform_grow", "noise", "noise_crossfade_s",
};
inline bool settings_key_has_value_list(std::string_view key) {
    for (const char* k : kSettingsValueListKeys)
        if (key == k) return true;
    return false;
}

struct GuiSettingsEditor {
    AppState&             app;
    GuiAudio&             audio;
    Viewport&             viewport;
    // The engine-key commit rebuilds the warp map, so it clears any resting
    // region AND the selection with it (architect 2026-07-29 — the
    // teardown is at that commit's tail). This member exists for that clear.
    Selection&            selection;
    Undo&                 undo;
    GuiTargetRender&   target_render;
    GuiPlaybackLifecycle& playback_lifecycle;
    // The card surface for the refusals this unit answers with a sentence:
    // every refused commit says its reason on a card, and the read-only lock
    // says its own at the engine-key commit arm (the lock governs the keys,
    // not the opener; the settings page's opener is admitted by the lock).
    GuiNotifications&     notifications;
    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds this editor by reference, so the
    // dependency is a cycle resolved with a pointer set post-construction —
    // the same shape as the viewport request_* callbacks). Used to reach the
    // live acts that live on GuiInputHandler: apply_gui_scale and the device
    // keys' apply_* (apply_true_colors and apply_waveform_grow).
    GuiInputHandler*      input = nullptr;

    GuiSettingsEditor(AppState&             app_,
                      GuiAudio&             audio_,
                      Viewport&             viewport_,
                      Selection&            selection_,
                      Undo&                 undo_,
                      GuiTargetRender&   target_render_,
                      GuiPlaybackLifecycle& playback_lifecycle_,
                      GuiNotifications&     notifications_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          target_render(target_render_),
          playback_lifecycle(playback_lifecycle_),
          notifications(notifications_) {}

    // THE ONE OPEN (the head, architect 2026-10-28; the value alone,
    // architect 2026-11-19): the editor on `key`, an editable row's key that
    // opens no value list (GuiInputHandler::settings_page_commit, its one
    // caller, after the page's close; asserted), its entry prefilled with
    // THE VALUE THE KEY HOLDS (setting_value, the writer's spelling), landed
    // as every Tab advance lands — through text_editor::replace_selection,
    // the one incoming filter, the caret at the VALUE'S END and nothing
    // selected. It RECORDS THE KEY (page_key, below).
    void open(const std::string& key);
    // THE OPEN'S KEY (architect 2026-10-28 night, the editor a sub-page of
    // the settings page; 2026-11-19, the key the commit writes):
    // recorded by open() and read by commit(), which composes the
    // assignment from it and the entry's value, and by the one return hook
    // (GuiInputHandler::handle_settings_editor_key), which reopens the page
    // on its row when the editor closes. Nothing typed changes it.
    const std::string& page_key() const { return page_key_; }
    // THE SETTINGS PAGE'S ROW TEXT for `key`: `key=` and the key's current
    // value as the recall gives it (setting_value below). Read by the
    // settings page's builder for each of kSettingsEditorKeys and for the
    // page's disabled `scale` and `bpm` (kSettingsPagePieceKeys, engine keys
    // the recall formats), every one of which recalls by construction (the
    // head's roads); a key that recalls nothing is a program bug and asserts.
    std::string setting_line(const std::string& key) const;
    // THE KEY'S CURRENT VALUE as the recall gives it, the writer's spelling
    // (recall_setting_value, settings_editor.cpp, this its one reader) —
    // setting_line's value side, the value list's seat (the row whose value
    // is this), the line editor's prefill at the open and Tab's stage 1.
    // Asserts for a key that recalls nothing, as setting_line does.
    std::string setting_value(const std::string& key) const;
    // THE VALUE LIST'S VALUES for `key`, one of kSettingsValueListKeys, in
    // the list's order, each value in the key's own spelling, which the
    // row shows, unitless, and its tap commits — the list's rows, one per
    // value (GuiInputHandler::build_value_list_rows): `true_colors`
    // kTrueColorsWords (`on`, `off`); `noise`
    // the folders under `noise/` beside the device config, sorted by byte —
    // settings_editor.cpp's folder_names for an empty prefix (directories
    // through a link, no dot-directory) — that the key's grammar
    // admits (is_noise_folder_name), so every row commits; no folder there,
    // or no config home, is a list with no rows; `noise_crossfade_s` 1 up
    // to 30; `waveform_grow` 0 up to 2 in quarters (2026-10-31). Every
    // value is the key's writer spelling (format_noise_crossfade_s,
    // format_waveform_grow),
    // so the seat compares it with setting_value byte for byte. Listed afresh
    // at each call; nothing is cached.
    std::vector<std::string> value_choices(const std::string& key) const;
    void exit_no_commit();
    void commit();
    // THE COMPLETION, run on BARE TAB with the focus in the field — the head's
    // two stages, on the open's key (page_key): on an EMPTY entry the value
    // the key holds (setting_value, the open's own prefill); on a non-empty
    // one the path key's filesystem (complete_path_value) and nothing for
    // every other key. The value recalled is byte-identical to what a Ctrl+S
    // (or, for a device key, the config's writer) would write, UTF-8
    // provenance text included (2026-08-02; the remaining edge is stated at
    // the definition).
    //
    // IT RETURNS WHETHER THE BUFFER CHANGED, which is the ONE AUTOCOMPLETE
    // MODEL's whole question (architect 2026-08-13: "we should use one model
    // for all autocompletes" — Tab offers the completion and is silent when
    // it did not advance, architect 2026-11-20; the rule is stated once at
    // route_modal_editor_key's Tab arm, input_key_dispatch.cpp). Every no-op answers false, so each is a Tab that
    // does nothing.
    bool autocomplete_value();

    // THE DEVICE KEYS' COMMIT — ONE BODY, TWO DOORS (architect 2026-10-25,
    // 2026-10-28 for the value lists' keys): the line editor's commit
    // (commit_device_setting, below, the editor's door) for `projects_repo`,
    // `projects_path` and `noise_attenuation_level`, and a VALUE LIST's pick
    // (GuiInputHandler::value_list_commit) for `true_colors`,
    // `waveform_grow`, `noise` and `noise_crossfade_s`, each key reaching it by
    // its one door since the line's key is its row's (architect 2026-11-19)
    // — one body per key (the head's item 1). The key's grammar decides, a
    // refusal printed and carded ("Settings edit rejected: <reason>"); a
    // value the key already holds is the unchanged no-op, a stderr line and
    // nothing written;
    // otherwise the live struct takes it, the key's live apply runs and the
    // device config is written (persist_device_config: a failure cards, the
    // live value standing for the session), with a card on success for
    // `projects_path` alone. THE EDITOR IS NOT TOUCHED HERE: the line's door
    // refuses or closes its field on the outcome, and the list's door
    // returns to the settings page whatever the outcome. NotDeviceKey for
    // any other key.
    enum class DeviceSettingOutcome { NotDeviceKey, Refused, Unchanged, Applied };
    DeviceSettingOutcome apply_device_setting(const std::string& key,
                                              const std::string& value);

    // THE DEVICE CONFIG'S WRITE, ADVISORY (the commit's persist): the whole
    // struct written (write_device_config, device_config.h); a failure prints
    // the diagnostic and cards the display, the live value standing either
    // way. Returns whether the file was written. THREE READERS: every device
    // key's commit (apply_device_setting's persist), the level case's end
    // (GuiInputHandler::settle_level_case: one write at a drag's lift or its
    // button-lost end, and at the tap's toggle — the level's one write, and
    // the restore level's with it, neither having a Settings row) and the
    // sticky lamps' comparator (main.cpp's refresh_lit_lamps, 2026-11-24:
    // one write on a tick whose lit set differs from the struct's).
    bool persist_device_config();

private:
    // THE LINE'S DOOR onto apply_device_setting (the head's item 1). Returns
    // true when `key` is a device key, the commit then fully handled — the
    // field refused and its text selected on a refusal, the editor closed on
    // an applied or unchanged value; false otherwise, so commit() goes on to
    // commit_gui_setting and the engine arm.
    bool commit_device_setting(const std::string& key,
                               const std::string& value);
    // THE PATH COMPLETER (2026-09-02), autocomplete_value's arm for the path
    // key — `projects_path` — when the entry is NON-EMPTY: the value is split
    // at its last `/`, the directory the head names is listed afresh
    // (absolute only, the grammar's own rule; a
    // relative head completes nothing, and so does a listing error), the
    // DIRECTORIES whose names start with the tail (byte compare — the key
    // names a folder) are the matches, and the buffer advances by their longest
    // common prefix past the tail, a single match taking a trailing `/`.
    // Nothing is cached. The append rides text_editor::replace_selection — the
    // one incoming filter — so the bytes are UTF-8-transparent exactly as a
    // paste is. Returns whether the buffer advanced, the one autocomplete
    // model's question; `value` is the entry less its leading whitespace
    // (ltrim_ws's reasons, settings_editor.cpp).
    bool complete_path_value(const std::string& value);
    // THE TWO KEYS OF THEIR OWN (the head's item 2): `gui_scale` and
    // `score_path`. Returns true for either, the commit then fully handled
    // inside (applied + deactivated, or refused with a card); false for any
    // other key, so commit() goes on to the engine arm. Each apply routes
    // through the key's own chokepoint — no parallel state writer.
    bool commit_gui_setting(const std::string& key, const std::string& value);
    // The open's key (page_key above); written by open() alone.
    std::string page_key_;
};
