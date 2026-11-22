#include "settings_editor.h"

#include "input_handler.h"
#include "render_output_naming.h"
#include "device_config.h"
#include "settings_io.h"
#include "warp_frame_map_view.h"  // the target-view re-land's two translations
#include "target_render.h"
#include "text_editor.h"
#include "undo.h"
#include "materials.h"
#include "score_pages.h"     // the score_path commit's read
#include "score_viewer.h"      // score_folder_changed
#include "score_viewer_file.h" // the score's five keys: grammar, recall, write-through

#include "frame_format.h"      // parse_authored_frame (the gui_scale arm)

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

// The commit's interactive latitude: strip surrounding whitespace from the
// entry's value (the FILE boundary is byte-exact and trims nothing); the
// empty-entry test of the Tab's recall reads it too.
//
// IT CANNOT SPLIT A MULTI-BYTE CHARACTER, which matters now that values carry
// UTF-8. `std::isspace` is consulted in the "C" locale — nothing in this
// program calls setlocale (locale_check.h states and enforces that) — where it
// is true for exactly 0x09..0x0d and 0x20, all ASCII. Every byte of a UTF-8
// multi-byte sequence is >= 0x80, so no continuation byte can be mistaken for
// whitespace and the trim always lands on a codepoint boundary.
std::string trim_ws(const std::string& s) {
    size_t a = 0;
    while (a < s.size() &&
           std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a &&
           std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// The leading half of the trim above, for the path completer alone: commit()
// trims both sides, so a value typed with a leading space is a path prefix
// like any other and Tab should complete it. The trailing side is deliberately
// left on, because the completer appends at the ENTRY'S end — a value that does
// not end where the buffer ends has nothing to append to, and letting the
// trailing bytes stay in the tail is what makes such a Tab match no name and
// do nothing instead of writing the completion past the gap.
std::string ltrim_ws(const std::string& s) {
    size_t a = 0;
    while (a < s.size() &&
           std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    return s.substr(a);
}

// THE PATH KEY — the device config's `projects_path`, the one key whose Tab
// completion is the filesystem's (complete_path_value). `projects_repo` is
// deliberately not one: a host/path, not a folder here.
bool is_path_completed_key(const std::string& key) {
    return key == "projects_path";
}


// The longest common prefix of the matches, cut back to a CODEPOINT BOUNDARY:
// two names sharing only the lead byte of different multi-byte characters
// would otherwise yield a lone lead byte, which the editor's incoming filter
// drops as malformed — the answer would be right (no advance) but by
// accident. The cut makes it right by construction: a UTF-8 lead byte says
// how many bytes its sequence has, and a prefix that ends inside one is
// shortened to the byte before it.
std::string common_prefix_on_codepoint_boundary(
        const std::vector<std::string>& names) {
    if (names.empty()) return {};
    std::string lcp = names.front();
    for (size_t i = 1; i < names.size(); ++i) {
        const std::string& n = names[i];
        size_t k = 0;
        while (k < lcp.size() && k < n.size() && lcp[k] == n[k]) ++k;
        lcp.resize(k);
    }
    // Walk back to the last lead byte (any byte that is not a continuation
    // byte) and drop the sequence it starts if that sequence is incomplete.
    size_t lead = lcp.size();
    while (lead > 0 &&
           (static_cast<unsigned char>(lcp[lead - 1]) & 0xc0) == 0x80) {
        --lead;
    }
    if (lead == 0) return {};
    const unsigned char b = static_cast<unsigned char>(lcp[lead - 1]);
    size_t expected = 1;
    if      ((b & 0xe0) == 0xc0) expected = 2;
    else if ((b & 0xf0) == 0xe0) expected = 3;
    else if ((b & 0xf8) == 0xf0) expected = 4;
    if (lcp.size() - (lead - 1) < expected) lcp.resize(lead - 1);
    return lcp;
}

// THE FOLDER-NAME LISTING — the path completer's and the noise value list's
// one listing body (2026-10-28, factored out of complete_path_value; the
// rules at their declarations): `dir` LISTED AFRESH
// AT EVERY CALL, with an error_code, so a directory that is not there, not
// readable or not a directory — or a listing error midway — lists nothing.
// The names are the DIRECTORIES whose names start with `tail` (byte compare
// — the key names a folder), in the listing's own order.
std::vector<std::string> folder_names(const std::filesystem::path& dir,
                                      const std::string& tail) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return {};
    std::vector<std::string> matches;
    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec) return {};
        const std::filesystem::directory_entry& e = *it;
        // DIRECTORIES ONLY — the key names a folder. Through the link: a
        // symlinked folder is a folder to name, and the grammar follows
        // nothing either way.
        std::error_code dec;
        if (!e.is_directory(dec) || dec) continue;
        const std::string name = e.path().filename().string();
        // Dot-directories are offered only when they are asked for, the
        // shell's own convention: nearly every home directory holds a
        // `.config` and a `.cache`, and with an empty tail those join the
        // match set and drag the common prefix to nothing, so the Tab that
        // most naturally asks "what is here" would advance by zero characters
        // and do nothing. A tail beginning with `.` says the
        // hidden names are the ones wanted and lets them all through.
        if (!name.empty() && name[0] == '.' &&
            (tail.empty() || tail[0] != '.')) continue;
        if (name.size() < tail.size() ||
            name.compare(0, tail.size(), tail) != 0) continue;
        matches.push_back(name);
    }
    return matches;
}

// THE FOLDER-NAME MATCH — the path completer's advance over that listing:
// a Tab that completes nothing is silent (the one autocomplete model).
// The answer is the matches' count and the ADVANCE past `tail`, their longest
// common prefix on a codepoint boundary less the tail (empty when the
// boundary cut took it to the tail or below: the matches diverge inside one
// character, or a single match is the tail already). The caller decides what
// a single match takes after it (a path's `/`).
struct FolderNameAdvance {
    std::size_t count = 0;
    std::string append;
};
FolderNameAdvance folder_name_advance(const std::filesystem::path& dir,
                                      const std::string& tail) {
    const std::vector<std::string> matches = folder_names(dir, tail);
    if (matches.empty()) return {};
    FolderNameAdvance out;
    out.count = matches.size();
    const std::string lcp = common_prefix_on_codepoint_boundary(matches);
    // The tail is a prefix of every match, so it is a prefix of the common
    // prefix — unless the boundary cut took it below the tail, in which case
    // the matches diverge inside one character and there is nothing to add.
    if (lcp.size() > tail.size()) out.append = lcp.substr(tail.size());
    return out;
}

// THE NOISE'S FOLDERS' HOME — `noise/` beside the device config
// (GuiNoise::probe's own composition, noise.h), read by the noise value
// list; empty with no config home, which lists nothing.
std::filesystem::path noise_home() {
    const std::filesystem::path config = device_config_path();
    if (config.empty()) return {};
    return config.parent_path() / "noise";
}

// THE BUFFER'S ADVANCE, the path completer's tail: `append` lands at the
// ENTRY'S END whatever the caret was doing, through the one incoming filter,
// as the recall's does; its over-capacity refusal is silent for the recall's
// reason (the text is the product's own, not the user's), and answers false
// because the buffer stood still.
bool append_at_line_end(AppState& app, Viewport& viewport,
                               const std::string& append) {
    const std::string before = app.settings_editor.pending;
    app.settings_editor.cursor_pos       =
        static_cast<int>(app.settings_editor.pending.size());
    app.settings_editor.selection_anchor = -1;
    (void)text_editor::replace_selection(app.settings_editor, append);
    viewport.invalidate_dialog_editor_box();
    return app.settings_editor.pending != before;
}

// THE RECALL — `key`'s current value as the writer writes it (the engine
// key's through format_engine_setting_value, the device keys' through
// recall_gui_setting_value, `score_path` through recall_score_viewer_value),
// or nothing for a key none of them writes. Its one reader is
// GuiSettingsEditor::setting_value, which asserts the answer: the page's
// rows and its disabled `scale` / `bpm` (setting_line), the value list's
// seat, the open's prefill and the empty entry's Tab.
std::optional<std::string> recall_setting_value(const AppState& app,
                                                const std::string& key) {
    std::optional<std::string> cur =
        format_engine_setting_value(app.engine_settings, key);
    if (!cur) cur = recall_gui_setting_value(app, key);
    if (!cur) cur = recall_score_viewer_value(app, key);
    return cur;
}

} // namespace

// The one opener, and it carries no read-only decision at all: the lock
// governs the keys, not the surface (architect 2026-09-04).
//
// The editor authors the per-device keys, which belong to no piece
// (device_config.h: no undo, no dirty, no Ctrl+S), as well as the piece's
// engine keys, which the lock protects by the ruling's own vocabulary
// (read_only_key_blocked, input_key_dispatch.cpp: read-only protects the
// authored musical content, the two marker stores and the engine settings,
// and nothing else). So the decision sits at each key's own commit arm: the
// engine-key arm in commit() refuses under the lock with the lock's own card
// and the selected text every other refusal there leaves, while
// commit_device_setting's keys, the `gui_scale` arm and the `score_path` arm
// commit regardless.
//
// THE OPENER IS LIVE UNDER EITHER LOCK, ONE LEVEL UP TOO (2026-10-21, the
// planner's ruling by the 2026-09-04 rule): bare `;` — the SETTINGS PAGE's
// opener since 2026-10-28, whose rows are this editor's one road
// (GuiInputHandler::settings_page_commit) — is on the read-only allowlist
// (read_only_key_blocked's is_settings_page), which the iteration gate
// inherits, and the band's Settings button — the chord's pointer spelling —
// stays lit under both, so a locked ACTIVE tab keeps its road to the device
// keys. The entry holds THE OPENED KEY'S VALUE ALONE (architect 2026-11-19:
// "One, agree."): nothing typed names another key, so no key
// without an editable row — a tab's read-only bit, the view keys, the
// per-tab band, the score's range and positions, `scale` and `bpm` — has a
// road through this editor; each has its own lever (the head). THE ONE LOCK
// ARM BELOW IS THE KEYS' OWN RULE: the engine-key arm's, which asks
// authoring_locked for both locks at once — the read-only bit and grid
// iterations (architect 2026-09-10) — the editable keys owing no other gate,
// none of them a chord either lock's keyboard gate refuses.
void GuiSettingsEditor::open(const std::string& key) {
    // ALREADY OPEN IS SILENT: the editor is on screen, which is the whole
    // answer. (Its one caller is the settings page's row, which no press
    // reaches under the editor's veil; the arm is the opener's own shape.)
    if (text_editor::is_active(app.settings_editor)) return;
    // THE KEY THE COMMIT WRITES AND THE ROW THE CLOSE RETURNS TO (page_key's
    // contract, settings_editor.h): an editable row's, by the one caller.
    assert(is_settings_editor_key(key) && !settings_key_has_value_list(key) &&
           "the line editor opens on an editable row's key");
    page_key_ = key;
    // THE MODAL PLAYBACK STOP IS THE OPENER'S, past the return above — the
    // `h` view's own load raise takes the same shape ("playback halts only
    // when the modal actually opens, so a refused open leaves a listening
    // session undisturbed"). It moved here from the two call sites in
    // 2026-08-07 with the read-only gate that has since left again: a
    // caller-side stop would have made a refused open kill a live audition and
    // open nothing.
    playback_lifecycle.stop_playback_for_modal_open();
    text_editor::enter(app.settings_editor,
                       /*target=*/0,
                       /*initial_pending=*/"",
                       text_editor::Kind::SettingsAssignment);
    // THE PREFILL IS THE VALUE THE KEY HOLDS (setting_value, the writer's
    // spelling) and it lands as every Tab advance lands (the head): through
    // the one incoming filter into the empty entry, so the caret stands at
    // the value's end with nothing selected. Its over-capacity refusal is
    // silent for the recall's reason (the text is the product's own recall,
    // autocomplete_value's account; kMaxPendingCharsSettings says what it
    // leaves).
    (void)text_editor::replace_selection(app.settings_editor,
                                         setting_value(key));
    // A MODAL OPEN damages the whole window: the modal's rect does not exist
    // before its first paint (the stash is the painter's), so the status-row
    // owner's modal rider cannot cover it here. Every later repaint of the
    // session rides that owner (viewport.cpp).
    viewport.invalidate_all();
}

std::string GuiSettingsEditor::setting_line(const std::string& key) const {
    return key + "=" + setting_value(key);
}

std::string GuiSettingsEditor::setting_value(const std::string& key) const {
    const std::optional<std::string> value = recall_setting_value(app, key);
    assert(value && "a settings page key recalls nothing");
    return *value;
}

// THE VALUE LISTS' ROWS (the domains at the declaration). Each arm is
// its key's own domain owner read whole — the switch's
// words, the noise's folder listing, the grow's quarters, the crossfade's
// seconds — with every value in the writer's spelling, so
// the row the tap commits is a value the key's grammar takes byte for byte
// and the live value's row is found by a byte compare. A
// row's text carries NO UNIT (architect 2026-10-28 night: "the units are not
// present in the setup main page, so let's remove it from the lists.
// consistent with linux unitless values (ffmpeg, etc)"): it is the value as
// the config and the page's own `key=value` row spell it, `on`, `4`, the unit
// living in the key's name and its grammar alone.
std::vector<std::string> GuiSettingsEditor::value_choices(
        const std::string& key) const {
    std::vector<std::string> out;
    if (key == "true_colors") {
        for (const char* word : kTrueColorsWords) out.emplace_back(word);
    } else if (key == "noise") {
        const std::filesystem::path home = noise_home();
        if (!home.empty()) {
            std::vector<std::string> names = folder_names(home, "");
            std::sort(names.begin(), names.end());
            for (std::string& name : names) {
                if (!is_noise_folder_name(name)) continue;
                out.push_back(std::move(name));
            }
        }
    } else if (key == "waveform_grow") {
        for (int n = kWaveformGrowMinQuarters; n <= kWaveformGrowMaxQuarters; ++n)
            out.push_back(format_waveform_grow(n));
    } else if (key == "noise_crossfade_s") {
        for (int s = kNoiseCrossfadeMinS; s <= kNoiseCrossfadeMaxS; ++s)
            out.push_back(format_noise_crossfade_s(s));
    } else {
        assert(false && "a key with no value list");
    }
    return out;
}

void GuiSettingsEditor::exit_no_commit() {
    if (!text_editor::is_active(app.settings_editor)) return;
    viewport.invalidate_modal_dialog_area();
    text_editor::deactivate(app.settings_editor);
}

// THE TWO KEYS OUTSIDE BOTH THE DEVICE BODY AND THE ENGINE ARM (the head's
// item 2): `gui_scale` and `score_path`, each through its own grammar owner
// and its own chokepoint — no parallel state writer, no grammar spelled
// twice. A refusal is the refused field AND A CARD with the owner's reason
// (2026-08-30: one composed sentence, the stderr line and the card its two
// readers). Neither touches undo history or dirty state (the scale is the
// device's, the score folder the score viewer's file's); a same-value
// commit no-op-deactivates like the engine no-op gate. Playback is already
// stopped (the editor is modal), so the appliers need no playback
// special-casing. Returns false for any other key, so the caller goes on to
// the engine arm.
bool GuiSettingsEditor::commit_gui_setting(const std::string& key,
                                           const std::string& value) {
    // ONE COMPOSER, TWO READERS (architect 2026-08-30): the refusal sentence
    // is built once and read by the stderr line and by the card, the refused
    // field saying nothing of why (its whole text selected, nothing more).
    auto reject = [&](const std::string& reason) {
        text_editor::refuse(app.settings_editor);
        viewport.invalidate_dialog_editor_box();
        const std::string refusal = "Settings edit rejected: " + reason;
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
    };
    auto applied = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: Setting applied: %s=%s\n",
            key.c_str(), value.c_str());
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.settings_editor);
    };
    auto unchanged = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: Setting unchanged: %s=%s\n",
            key.c_str(), value.c_str());
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.settings_editor);
    };

    // THE DEVICE CONFIG'S SCALE (2026-08-27): `gui_scale` is a per-DEVICE
    // preference living in its own file, and this editor is its authoring
    // surface. THE GRAMMAR IS SPELLED ONCE: the canonical integer spelling
    // through parse_authored_frame and the RANGE through is_gui_scale_percent
    // (device_config.h), which is the very predicate that file's reader runs,
    // so "loadable iff it commits" holds. (It stays out of
    // commit_device_setting's body because it HAS a chokepoint of its own,
    // apply_gui_scale, which writes the device config itself.)
    if (key == "gui_scale") {
        int64_t v64 = 0;
        if (!parse_authored_frame(value, v64) || !is_gui_scale_percent(v64)) {
            reject(kGuiScaleGrammarReason);
            return true;
        }
        // History-less, and APPLIED LIVE: the store write is no longer the
        // whole commit — since
        // 2026-07-31 the redesigned rows size on this value, so the commit
        // pushes it to the renderer and re-lays-out immediately (apply_gui_scale
        // runs the resize-path rebuild), and a `gui_scale=200` is visible
        // without a restart. It marks nothing dirty and Ctrl+S does not carry
        // it: since 2026-08-27 the applier WRITES THE DEVICE CONFIG at the
        // commit, so the value is persisted the moment it is applied.
        const int v = static_cast<int>(v64);
        if (v == app.gui_scale) { unchanged(); return true; }
        input->apply_gui_scale(v);
        applied(); return true;
    }

    // THE SCORE'S FOLDER (architect 2026-10-26 night), the one key of the
    // score viewer's own file this editor writes (the range and the
    // positions are the viewer's own, its START / END and its pages):
    // its grammar owner is that file's (validate_score_viewer_value,
    // score_viewer_file.h), and a committed change WRITES THAT FILE THROUGH
    // at once (the device keys' road, never the save's).
    if (key == "score_path") {
        const auto sv = validate_score_viewer_value(key, value);
        assert(sv && "score_path is the score viewer file's key");
        if (!*sv) {
            reject(sv->error());
            return true;
        }
        const GuiScoreViewerValue& v = **sv;
        // THE FOLDER: the commit READS IT, the load's own road
        // (score_pages.h's three outcomes), so a typed path is the score on
        // screen at the Enter — the SCORE key lit or grey by the next frame. A
        // MALFORMED pages.txt refuses here with the reader's own reason, the
        // editor's immediate courtesy (the load would take it as no score
        // and one stderr line); an ABSENT folder commits, the key grey and a
        // card saying where nothing was found (the folder may arrive by the
        // sync script later). The empty value is no score.
        if (v.text == app.score_path) { unchanged(); return true; }
        if (v.text.empty()) {
            app.score.reset();
            app.score_path.clear();
            score_folder_changed(app);
            write_score_viewer_file(app);
            applied(); return true;
        }
        const std::filesystem::path folder = score_folder_of(
            score_viewer_project_folder(app), v.text);
        auto score = read_score_folder(folder);
        if (!score) {
            reject("invalid score in '" +
                   shown_project_path(folder / "pages.txt") + "': " +
                   score.error());
            return true;
        }
        app.score_path = v.text;
        write_score_viewer_file(app);
        if (score->pages) {
            app.score = std::move(score->pages);
        } else {
            app.score.reset();
            std::fprintf(stderr, "warptempo_gui: %s; no score\n",
                         score->advisory.c_str());
            notifications.notify(AppState::NotificationClass::Normal,
                                 "No score at '" +
                                     shown_project_path(folder) + "'");
        }
        // The soundtrack and the page cache follow the folder
        // (score_folder_changed: the old load joined, the new one
        // started on its worker).
        score_folder_changed(app);
        applied(); return true;
    }

    return false;
}

void GuiSettingsEditor::commit() {
    if (!text_editor::is_active(app.settings_editor)) return;

    // THE ASSIGNMENT IS COMPOSED, NEVER PARSED (architect 2026-11-19, send
    // AA: "One, agree."): the key is the open's (page_key_, an editable
    // row's), the value the entry's whole text with its surrounding
    // whitespace trimmed (trim_ws, the interactive latitude; the FILE
    // boundary trims nothing). Nothing is split, so a value holding `=` or
    // spaces — a url with a `?v=` query, a notes line — commits intact, and
    // nothing typed can name another key.
    const std::string& key   = page_key_;
    const std::string  value = trim_ws(app.settings_editor.pending);

    // 1. The device keys other than the scale — one body, ahead of the
    //    routers (the head's item 1; the body's own comment carries the
    //    rest).
    if (commit_device_setting(key, value)) return;

    // 2. `gui_scale` and `score_path` (the head's item 2): fully handled
    //    inside, applied and closed or refused with a card; false for the
    //    piece's engine keys, which go on to the engine arm.
    if (commit_gui_setting(key, value)) return;

    // 3. The engine-key write (the head's item 3): the piece's four editable
    // keys, every other editable row's key having been taken above, so the
    // key is a canonical engine key by construction. The value is validated
    // through the same helper the file-load deserializer uses;
    // capture-before-mutate so the snapshot on the undo stack reflects the
    // pre-edit settings.
    assert(is_canonical_engine_key(key) &&
           "an editable row's key that no arm above takes is an engine key");

    // The lock, and this is the whole of it for this surface (architect
    // 2026-09-04, moving the decision off GuiSettingsEditor::open — the
    // account is there). An engine key IS the piece: read-only protects the
    // authored musical content, the two marker stores and the engine settings
    // (read_only_key_blocked, input_key_dispatch.cpp), so the four editable
    // engine keys (`title`, `notes`, `url`, `cover`) refuse here while the
    // device keys, `gui_scale` and `score_path` above commit on a locked tab.
    // It sits ahead of the value validator because the lock outranks the
    // grammar: a locked tab refuses the key whatever the value would have
    // been. The refused field plus card, the shape every refusal on this
    // surface wears.
    //
    // IT IS BOTH LOCKS SINCE 2026-09-10 (authoring_locked, app_state.h): an
    // engine key is a map input and its commit pushes a settings undo entry,
    // so grid iterations refuses it exactly as the read-only bit does, and the
    // SAME division holds — the keys above still commit under a lit lamp,
    // because the lock governs the KEYS and not the surface.
    // The sentence forks at the one composer (authoring_lock_card,
    // notifications.h) because this site knows its act and needs no chord in
    // it. (The editor opens under either lock — bare `;` and the SETUP
    // key open the settings page, which both locks admit, and a row opens
    // the editor (2026-10-28) — so this arm is where a locked tab's engine
    // key refuses, the open's account.)
    if (authoring_locked(app)) {
        const char* card = authoring_lock_card(app);
        text_editor::refuse(app.settings_editor);
        viewport.invalidate_dialog_editor_box();
        std::fprintf(stderr, "warptempo_gui: %s\n", card);
        notifications.notify(AppState::NotificationClass::Normal, card);
        return;
    }

    EngineSettings candidate = app.engine_settings;
    std::string reason;
    if (!validate_engine_setting(key, value, candidate, reason)) {
        text_editor::refuse(app.settings_editor);
        viewport.invalidate_dialog_editor_box();
        const std::string refusal =
            "Settings edit rejected: key '" + key + "' has invalid value '" +
            value + "': " + reason;
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return;
    }

    // Source-clobber guard. An edit that makes the title-named render output
    // resolve to the source file itself would overwrite the source on the next
    // render, and the shared predicate composes and checks that path. What is
    // still reachable now that the deliverable lands one folder down, in the
    // project's `render/`, is stated at the predicate itself
    // (render_output_source_collision, render_output_naming.h): not a
    // spelling, but a `render/` or a `render/<title>.wav` that is linked onto
    // the source. Refused here so the colliding value never reaches
    // app.engine_settings.
    if (render_output_source_collision(candidate, app.source_audio_path)) {
        text_editor::refuse(app.settings_editor);
        viewport.invalidate_dialog_editor_box();
        // The path names its BASENAME on the card, SINGLE-QUOTED as every
        // name is (notifications.h's card rules), and the stderr line has always named the
        // same.
        // ONE CLAUSE AND NO INSTRUCTION AFTER IT (2026-09-01, the
        // capitalization sweep's sentence shape): the reason IS the message,
        // the grid-iterations cap card's own rule. It closed with "; choose a
        // different title" until that day.
        const std::string refusal =
            "Settings edit rejected: this would make the render output "
            "overwrite the source file '" +
            std::filesystem::path(app.source_audio_path).filename().string() +
            "'";
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return;
    }

    // No-op gate. An undo entry represents a state change, not a gesture, so
    // committing the value already in effect deactivates the editor and
    // touches no history, dirty state, view, or render. The key has passed
    // is_canonical_engine_key, so both serializations are engaged; comparing
    // canonical serialized bytes makes every accepted spelling of the current
    // value a no-op (for example a scale written with extra trailing zeros).
    std::optional<std::string> cur_serialized =
        format_engine_setting_value(app.engine_settings, key);
    std::optional<std::string> new_serialized =
        format_engine_setting_value(candidate, key);
    if (cur_serialized == new_serialized) {
        std::fprintf(stderr,
            "warptempo_gui: Setting unchanged: %s=%s\n",
            key.c_str(), cur_serialized->c_str());
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.settings_editor);
        return;
    }

    // THE PLAYHEAD'S OWN MUSICAL INSTANT, in SOURCE frames and read while the
    // OLD map still stands — the subject of this commit's target-view re-land
    // below, and the delete's own two lines in the warp family's shape (the
    // contract at the head of warpmarkers_ops.cpp). The engine scale is a
    // warp-map input, so a commit that moves it re-warps the target domain
    // under a resting cursor; like the delete, this act leaves NO FOCUS to
    // follow (the selection clears below), so the cursor's own instant is what
    // has to survive the rewrite. active_domain_to_source_frame is the
    // product's one inverse for a bare frame — the identity in source view,
    // the memoized target map's inverse in target view — so this costs two
    // compares off home and is read unconditionally.
    const int64_t playhead_source_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);

    SettingsSnapshot pre = capture_current_settings(app);
    app.engine_settings = std::move(candidate);
    undo.push_settings_undo(std::move(pre));

    std::fprintf(stderr,
        "warptempo_gui: Setting applied: %s=%s\n",
        key.c_str(), value.c_str());

    viewport.invalidate_modal_dialog_area();
    text_editor::deactivate(app.settings_editor);
    // (THE ENGINE COMMIT'S WHOLESALE OVERLAY HIDE IS DELETED, 2026-08-19: the
    // region is the trim, whose source-domain bounds the re-warp below leaves
    // untouched — the trim bar and its endcaps go on showing it in the new
    // domain, with nothing stale to put away.)
    // THE SELECTION STILL GOES (architect 2026-07-29): an engine
    // commit rebuilds the map under every marker INDEX and IMAGE at once, so no
    // marker keeps the identity a focus named — the same
    // "ready to move on" act the trim setters make when a trim-bar click deselects.
    // This is the SYMMETRIC half of a pair: the settings-only ('S') undo/redo
    // restore clears the selection at its own restore (undo.cpp), and GUI-kind
    // keys are
    // history-less, so no other settings entry kind exists to cover. Together they
    // are what let the never-span-less ENFORCEMENT be deleted: this site was one of
    // its two remaining producers, and closing it here means no collapse protocol
    // is needed rather than a collapse being owed. It touches no trim: a
    // selection mutator writes no trim state at all.
    selection.clear_selection();
    // Full-area damage for the teardown, and it is the site's own: clear_selection
    // damages what its flag rebuild changed alone (the stems, the
    // ring), and an already-empty selection
    // raises nothing there, while this commit rebuilds the map under the whole
    // plate either way (the kick below). A settings commit is a rare, discrete
    // command, so it takes the standing "rare command, full damage" answer.
    viewport.invalidate_waveform_area();
    // The engine scale is a warp-map input (build_warp_frame_map's slope
    // product), so an engine commit that moved it re-warps the target-view
    // plate. This is one of the target-view re-warp sites (the full inventory
    // lives at Viewport::kick_waveform_sync; warp placement edits author in source
    // view only under the home-view binding), so re-warp synchronously in target
    // view so displayed == live at this command boundary. A non-scale engine key (provenance) leaves the plate
    // unchanged, so the sync is a redundant bounded rebuild there — acceptable,
    // matching the unconditional trigger beside it.
    // AND THE PLAYHEAD RE-LANDS ON ITS OWN INSTANT (architect 2026-09-02,
    // superseding the "NO RE-LAND, and none is
    // possible" this site carried from 2026-07-29). That reading was the
    // pre-2026-08-24 FOCUS argument: with the selection cleared above there is
    // no focus whose image could be the subject, so the site concluded there
    // was no subject at all. The DELETE had already answered it — it clears the
    // selection too and re-lands the PLAYHEAD'S OWN MUSICAL INSTANT, captured
    // in source frames before the rewrite (warpmarkers_ops.cpp) — and a whole-
    // map rewrite is exactly the delete's class: keeping the cursor's NUMBER
    // across it moves the cursor in the music, which is what the family's
    // re-land exists to prevent, and left an edit-then-undo landing the cursor
    // where neither the edit nor the undo put it. THROUGH THE RESEAT, never a
    // movement owner: an image moving out from under a resting cursor is a
    // TRANSLATION, not a movement in the music (the rule at
    // Viewport::move_playhead_to, viewport.cpp). Source view needs nothing —
    // the identity domain, where the inverse above and this forward map are
    // both identity and the write is the value the cursor already holds.
    if (app.active_audio_view == 'T') {
        viewport.kick_waveform_sync();
        viewport.reseat_playhead_to(
            source_frame_to_active_domain(app, audio, playhead_source_frame));
    }
    // The trigger is unconditional by ruling — rationale recorded at
    // GuiTargetRender::trigger. Under the full-recipe key every engine-settings
    // commit moves the fingerprint, provenance (title/bpm/notes/url/cover)
    // included, so the target-view re-preview renders fresh rather than reusing
    // the prior buffer. Playback is not at stake at this site: the editor is
    // modal and its open already stopped playback.
    target_render.trigger();
}

// THE DEVICE KEYS' COMMIT (the head's item 1). projects_repo has taken
// a direct-set arm here since it was a `.settings` key — free text, no undo
// history, no dirty tracking, the settings editor its sole authoring surface
// — and since 2026-08-27 it is a DEVICE preference: ONE user has ONE
// repository, so the projects home is the device config's (device_config.h)
// and Ctrl+S does not carry it. projects_path JOINED IT 2026-09-02
// (architect — the Settings dropdown carried
// both as rows until 2026-10-21), which is what made the arm a body: ONE SHAPE FOR THE TWO.
// The key's own grammar owner in device_config.h decides —
// is_projects_repo, is_projects_path, never a second spelling
// — and a refusal is the refused field and the card with the grammar's own
// reason, through the composer every other key uses. A value byte-equal to
// the live one is the consumed no-op every key of this editor takes (the
// empty value included) and writes nothing. On success the LIVE STRUCT takes
// the value (AppState::device_config, the loop's one — so the other keys
// travel as they stand, the ownership rule at write_device_config) and THIS
// COMMIT IS THE PERSIST: the config is written right here. A failed write is
// advisory — the live value stands for the session — and since 2026-09-02 it
// is SAID: the writer composes the two clauses at its one failure point
// (GuiFailure, failure.h) and this site prints the diagnostic and cards the
// display, the strictness ruling's shape for a press whose result nothing
// paints.
//
// THE BODY'S INTEGER ARMS CAME AND WENT: the waveform's height cap
// (2026-09-13..10-07, its live relayout with it — the waveform is the
// lanes' leftover since, main.cpp's vertical rule) and the hold delay's key
// for its one-day tuning phase (architect 2026-09-29, the value hard-coded
// as kHoldDelayMs, gui_input.h), each struck with its Settings row.
//
// WHEN EACH IS IN FORCE. `projects_repo`: at once — every reader reads
// `app.projects_repo`, the live field, whose source moved 2026-08-27 and whose
// readers did not (an empty value simply never matches any remote, which
// disables the GitHub recheck). `projects_path`: FOR THE NEXT OPEN PROJECT AND THE
// NEXT LAUNCH, the open project staying open — the picker lists the folders
// under the live struct's path and gui_main's reopen resolves the chosen name
// under that same live value (main.cpp), so Open Project already
// works against the new folder, while the open project's own paths are
// absolute and untouched. The card says where the change applies, because
// nothing on screen changes at the commit. `last_project` still names a
// folder under the OLD path until the next successful open rewrites it; the
// next launch's fallback (startup_source, project_model.cpp) is load-lenient
// on a name that is gone and takes the first valid folder under the new path.
// THE ONE RECORDED EDGE: the picker's same-project no-op compares NAMES
// (open_project_commit), so a folder under the new path carrying the open
// project's own name reads as "already open" until a relaunch or a different
// project is opened first — a rename-by-hand case, accepted.
// `true_colors` (2026-10-21, the popup's command row made a key): AT ONCE,
// live, no card — its arm below; `waveform_grow` (2026-10-31) the same, its arm after
// that, `noise` and `noise_crossfade_s` (2026-10-28) and
// `noise_attenuation_level` (2026-11-05 night) the same, at once and no
// card, their arms after it (the level, `noise_level`, has no arm since
// 2026-11-05 night: no Settings row, the band's NOISE case its one road,
// kSettingsEditorKeys), and the keyboard's three tuning keys `osk_key_w`,
// `osk_key_h` and `osk_font_px` (2026-10-29) the same, their arms last
// (every retired key's arm left with its key, the record git's). The
// commit is the line editor's Enter for the keys whose row opens it,
// and a list row's tap for the four keys with a value list
// (kSettingsValueListKeys, settings_editor.h), whose rows open the list and
// never the line editor — ONE BODY BEHIND BOTH DOORS (apply_device_setting
// below; the choice rows' list rows of 2026-10-07..10-14, which showed a
// value and committed nothing, went with the choice editor).
//
// THE LINE'S DOOR (the contract at the declaration): the one body decides,
// and this door does to the field what the outcome says — the refused field
// with its text selected, or the editor closed. The key is the open's, one
// of the line editor's rows (architect 2026-11-19), so the four
// listed keys' arms below are reached through the list's door alone.
bool GuiSettingsEditor::commit_device_setting(const std::string& key,
                                              const std::string& value) {
    switch (apply_device_setting(key, value)) {
    case DeviceSettingOutcome::NotDeviceKey:
        return false;
    case DeviceSettingOutcome::Refused:
        text_editor::refuse(app.settings_editor);
        viewport.invalidate_dialog_editor_box();
        return true;
    case DeviceSettingOutcome::Unchanged:
    case DeviceSettingOutcome::Applied:
        viewport.invalidate_modal_dialog_area();
        text_editor::deactivate(app.settings_editor);
        return true;
    }
    return true;
}

GuiSettingsEditor::DeviceSettingOutcome
GuiSettingsEditor::apply_device_setting(const std::string& key,
                                        const std::string& value) {
    // ONE COMPOSER, TWO READERS — commit_gui_setting's own shape, the
    // `gui_scale` arm's sentence exactly (the reason after the tag and no key
    // name, the field on screen — or the list's row — already showing which
    // key): the refusal sentence is built once and read by the stderr line
    // and by the card, the refused field saying nothing of why.
    auto reject = [&](const char* reason) {
        const std::string refusal =
            std::string("Settings edit rejected: ") + reason;
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return DeviceSettingOutcome::Refused;
    };
    auto unchanged = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: Setting unchanged: %s=%s\n",
            key.c_str(), value.c_str());
        return DeviceSettingOutcome::Unchanged;
    };
    // The write is advisory in the ruling's sense: it may fail, the live value
    // stands either way, and the failure is the press's answer on a card
    // (persist_device_config).
    auto persist = [&]() -> bool { return persist_device_config(); };
    auto applied = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: Setting applied: %s=%s\n",
            key.c_str(), value.c_str());
        return DeviceSettingOutcome::Applied;
    };

    // TRUE COLORS (architect 2026-10-21, the Settings popup's "True Colors"
    // command row made a key): one of the two words under its one owner
    // (parse_true_colors, device_config.h), APPLIED LIVE AT THE COMMIT, which
    // is the line editor's Enter or the value list's row tap, through
    // the toggle's own apply shape (GuiInputHandler::apply_true_colors — the
    // switch's bit, the icon rasters, the plate or the flag cache, the whole
    // window), so NO CARD: the colors on screen are the
    // answer; a failed write keeps the live value for the session, the card
    // saying the persist failed, and a value that leaves the file as it
    // stands is the unchanged no-op.
    if (key == "true_colors") {
        const std::optional<bool> on = parse_true_colors(value);
        if (!on) return reject(kTrueColorsGrammarReason);
        if (*on == app.device_config->true_colors) return unchanged();
        app.device_config->true_colors = *on;
        input->apply_true_colors(*on);
        (void)persist();
        return applied();
    }

    // THE NOISE'S FOLDER (architect 2026-10-28; noise.h owns the sound): one
    // folder name under its one grammar owner (is_noise_folder_name,
    // device_config.h), the value trimmed as every string key's; a same-name
    // commit is the unchanged no-op, so GuiNoise::change_folder never sees one.
    // Otherwise the live struct takes the name and the one GuiNoise CHANGES
    // FOLDER — THE MEMORY IS DUMPED ON EVERY NAME CHANGE, audible or not (his
    // ruling: "whatever's in memory is dumped if we change the subfolder
    // name"), the new folder probed and, under an audible level, loaded at once
    // (the level is the switch, noise.h) — and the file is written. NO CARD:
    // the NOISE case's face is the answer, live with a folder there and grey
    // when it is absent or refused, exactly as a launch with that name would
    // show (the absent-folder rule, noise.h). Its value list is its one door
    // (recall_gui_setting_value the list's seat, the name verbatim).
    if (key == "noise") {
        if (!is_noise_folder_name(value))
            return reject(kNoiseFolderGrammarReason);
        if (value == app.device_config->noise) return unchanged();
        app.device_config->noise = value;
        if (app.noise != nullptr) app.noise->change_folder(value);
        (void)persist();
        return applied();
    }

    // THE WAVEFORM PLATE'S GROW (architect 2026-10-31, a permanent key;
    // render.h's `waveform_grow` seat): a multiple of 0.25 device px under its
    // one grammar owner (parse_waveform_grow, device_config.h), APPLIED LIVE AT
    // THE COMMIT — the live struct, then the one live act
    // (GuiInputHandler::apply_waveform_grow: the renderer's seat and the window
    // repainted, no plate re-render — the grow is the GPU's alone) — and
    // persisted, so NO CARD: the waveform is the answer. The page opens its
    // VALUE LIST (9 values, kSettingsValueListKeys), its one door
    // (recall_gui_setting_value the list's seat).
    if (key == "waveform_grow") {
        const std::optional<int> quarters = parse_waveform_grow(value);
        if (!quarters) return reject(kWaveformGrowGrammarReason);
        if (*quarters == app.device_config->waveform_grow_quarters)
            return unchanged();
        app.device_config->waveform_grow_quarters = *quarters;
        input->apply_waveform_grow(*quarters);
        (void)persist();
        return applied();
    }

    // THE NOISE'S ATTENUATION (architect 2026-11-05 night, session 49: "then
    // the slider maintains its current behavior and law"; noise.h's head): a
    // fixed dB under the NOISE case's number, the bed at noise_level + this,
    // under its one grammar owner (parse_noise_attenuation_db, device_config.h:
    // THE LEVEL GRAMMAR less -inf, the bed's switch being the level's), APPLIED
    // LIVE AT THE COMMIT — the live struct, then the one GuiNoise's setter (the
    // gain recomputed from both, the switch untouched: the attenuation never
    // lights or darkens the voice) — and persisted, so NO CARD: the sound is
    // the answer. A value the key already holds is the unchanged no-op (the
    // exact double: "-9.0" for "-9"). The page opens the line editor on it (no
    // value list, a decimal); Tab on an empty entry recalls the writer's
    // spelling (recall_gui_setting_value).
    if (key == "noise_attenuation_level") {
        const std::optional<double> db = parse_noise_attenuation_db(value);
        if (!db) return reject(kNoiseAttenuationGrammarReason);
        if (*db == app.device_config->noise_attenuation_level)
            return unchanged();
        app.device_config->noise_attenuation_level = *db;
        if (app.noise != nullptr) app.noise->set_attenuation_db(*db);
        (void)persist();
        return applied();
    }

    // THE NOISE'S CROSSFADE (architect 2026-10-28; noise.h owns the sound): an
    // integer under its one grammar owner (parse_noise_crossfade_s,
    // device_config.h), APPLIED LIVE AT THE COMMIT — the live struct, then the
    // one GuiNoise's setter (the crossfade, in the key's seconds, at the next
    // transition's draw) — and persisted, so NO CARD: the sound is the answer.
    // Its value list is its one door (recall_gui_setting_value the list's
    // seat).
    if (key == "noise_crossfade_s") {
        const std::optional<int> s = parse_noise_crossfade_s(value);
        if (!s) return reject(kNoiseCrossfadeGrammarReason);
        if (*s == app.device_config->noise_crossfade_s) return unchanged();
        app.device_config->noise_crossfade_s = *s;
        if (app.noise != nullptr) app.noise->set_crossfade_s(*s);
        (void)persist();
        return applied();
    }

    // THE ON-SCREEN KEYBOARD'S KEY BOX (architect 2026-10-29, two tuning keys;
    // render.h's `osk_key_w` / `osk_key_h` seat): each an integer under the one
    // grammar owner (parse_osk_key_size, device_config.h), APPLIED LIVE AT THE
    // COMMIT — the live struct, then the one live act
    // (GuiInputHandler::apply_osk_key_size: the renderer's seat and the window
    // repainted at once, the box moving the keyboard's surface alone, which
    // moves no lane) — and persisted, so NO CARD: the keyboard standing
    // under this very editor is the answer. The page opens the line editor
    // on each (no value list, 69 values no short domain); Tab on an empty
    // entry recalls the writer's spelling (recall_gui_setting_value).
    if (key == "osk_key_w" || key == "osk_key_h") {
        const std::optional<int> px = parse_osk_key_size(value);
        if (!px) return reject(kOskKeySizeGrammarReason);
        int& live = key == "osk_key_w" ? app.device_config->osk_key_w
                                       : app.device_config->osk_key_h;
        if (*px == live) return unchanged();
        live = *px;
        input->apply_osk_key_size(app.device_config->osk_key_w,
                                  app.device_config->osk_key_h);
        (void)persist();
        return applied();
    }

    // THE ON-SCREEN KEYBOARD'S CAP SIZE (architect 2026-10-29 night, a tuning
    // key; render.h's `osk_font_px` seat): an integer under its one grammar
    // owner (parse_osk_font_px, device_config.h), APPLIED LIVE AT THE COMMIT —
    // the live struct, then the one live act
    // (GuiInputHandler::apply_osk_font_px: the renderer's seat and the window
    // repainted, no relayout) — and persisted, so NO CARD: the keyboard
    // standing under this very editor is the answer. The page opens the line
    // editor on it (no value list, 19 values no short domain); Tab on an empty
    // entry recalls the writer's spelling (recall_gui_setting_value).
    if (key == "osk_font_px") {
        const std::optional<int> px = parse_osk_font_px(value);
        if (!px) return reject(kOskFontPxGrammarReason);
        if (*px == app.device_config->osk_font_px) return unchanged();
        app.device_config->osk_font_px = *px;
        input->apply_osk_font_px(*px);
        (void)persist();
        return applied();
    }

    std::string* live    = nullptr;
    bool (*grammar)(const std::string&) = nullptr;
    const char* reason   = nullptr;
    if (key == "projects_repo") {
        live = &app.device_config->projects_repo;
        grammar = &is_projects_repo;
        reason  = kProjectsRepoGrammarReason;
    } else if (key == "projects_path") {
        live = &app.device_config->projects_path;
        grammar = &is_projects_path;
        reason  = kProjectsPathGrammarReason;
    } else {
        return DeviceSettingOutcome::NotDeviceKey;
    }

    if (!grammar(value)) return reject(reason);
    if (value == *live) return unchanged();
    *live = value;
    // The live field every reader of the repository reads (the rationale at
    // AppState::projects_repo); the path key has no field beside the
    // struct's own.
    if (key == "projects_repo") app.projects_repo = value;
    // The persist's verdict is kept because the applies-card below is a claim
    // about the next launch, which only a written file can make true — a
    // failed write leaves the old path on disk, so the two cards together
    // would have said the persist failed and then that it survives a
    // relaunch, with the second one topmost. On failure the device-config card
    // is the whole answer.
    const bool written = persist();
    if (key == "projects_path" && written) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kProjectsPathAppliesCard);
    }
    return applied();
}

bool GuiSettingsEditor::persist_device_config() {
    const auto failure = write_device_config(*app.device_config);
    if (failure) {
        std::fprintf(stderr, "warptempo_gui: %s\n",
                     failure->diagnostic.c_str());
        notifications.notify(AppState::NotificationClass::Normal,
                             failure->display);
    }
    return !failure;
}

bool GuiSettingsEditor::complete_path_value(const std::string& value) {
    // Split at the last `/`: the HEAD names the directory to list (the
    // separator kept, so `/` alone lists the root), the TAIL is what a name
    // must start with. A value with no `/` at all has no head, and a relative
    // head completes nothing — the grammar's own rule (is_config_path_value:
    // the key takes an absolute path), so the completer never offers a value
    // the commit would refuse.
    const size_t slash = value.rfind('/');
    if (slash == std::string::npos) return false;
    const std::string head = value.substr(0, slash + 1);
    const std::string tail = value.substr(slash + 1);
    const std::filesystem::path dir(head);
    if (!dir.is_absolute()) return false;

    // The listing, the directories-only and hidden rules and the prefix
    // (folder_name_advance, the shared body); a single match here takes a
    // trailing `/`, the value being a path that may go on.
    const FolderNameAdvance found = folder_name_advance(dir, tail);
    if (found.count == 0) return false;
    std::string append = found.append;
    if (found.count == 1) append += '/';
    if (append.empty()) return false;

    // The append lands at the LINE'S END (append_at_line_end, the
    // completers' shared tail).
    return append_at_line_end(app, viewport, append);
}

// Returns whether the buffer CHANGED — the one autocomplete model's question
// (the rule is stated at route_modal_editor_key's Tab arm,
// input_key_dispatch.cpp): the Tab that ran it is consumed either way, a
// false answer a Tab that did nothing. Every refusal below answers false.
bool GuiSettingsEditor::autocomplete_value() {
    if (!text_editor::is_active(app.settings_editor)) return false;
    const std::string pending = app.settings_editor.pending;

    // A NON-EMPTY ENTRY (the head's stage 2): the path key's value is a path
    // PREFIX, completed against the filesystem (complete_path_value, its
    // no-advance cases silent Tabs); every other key's typed value
    // advances nothing. Leading whitespace off, trailing whitespace kept —
    // the reasons are at ltrim_ws (commit() trims both sides, so a
    // `  /home` completes as the value it commits as).
    if (!trim_ws(pending).empty()) {
        if (is_path_completed_key(page_key_))
            return complete_path_value(ltrim_ws(pending));
        return false;
    }

    // AN EMPTY ENTRY (the head's stage 1; whitespace-only counts as empty):
    // THE VALUE THE KEY HOLDS (setting_value, the open's own prefill), so a
    // value cleared by hand comes back with one Tab. THIS is what makes a
    // SECOND Tab silent with no state to remember the first: the recall just
    // written is a non-empty entry, and a non-path key's non-empty entry
    // advances nothing.
    //
    // THE RECALL PRESERVES EVERY BYTE OF A VALUE, NO UTF-8 EXCEPTION (architect
    // 2026-08-02, the UTF-8 relaxation). The seed below lands through
    // text_editor::replace_selection, whose filter passes well-formed UTF-8
    // verbatim — and the free-text provenance values (title/notes/url/cover)
    // are exactly what the strict load takes verbatim (engine_settings_io.cpp),
    // so a value carrying non-ASCII text recalls unchanged and commits back
    // unchanged. What the filter drops is ASCII control bytes and malformed
    // byte sequences: not text, authorable by nothing in the product (the typed
    // path encodes real codepoints and the file writer writes what it held),
    // and reachable only by hand-editing a value into a state the GUI cannot
    // express.
    app.settings_editor.pending.clear();
    app.settings_editor.cursor_pos       = 0;
    app.settings_editor.selection_anchor = -1;
    // THE RECALL'S OWN OVER-CAPACITY REFUSAL IS SILENT, and it is one of the
    // replace_selection callers that say nothing (2026-08-30; the open's
    // prefill the other). The strictness ruling's two capacity sentences
    // answer what the USER just supplied — a typed character, a pasted
    // clipboard — while this text is the product's own recall of a value it
    // already holds (kMaxPendingCharsSettings says what it leaves).
    (void)text_editor::replace_selection(app.settings_editor,
                                         setting_value(page_key_));

    viewport.invalidate_dialog_editor_box();
    // The literal answer, compared against the buffer this call started from:
    // recalling an EMPTY value (a blank free-text key such as an empty `url`
    // or `projects_repo`) onto an empty entry writes nothing and is honestly
    // no advance, a silent Tab; a whitespace-only entry recalled empty is
    // a change (the blanks cleared), an advance.
    return app.settings_editor.pending != pending;
}
