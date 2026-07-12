#include "settings_editor.h"

#include "render_output_naming.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "target_render.h"
#include "text_editor.h"
#include "undo.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace {

std::string trim_ws(const std::string& s) {
    size_t a = 0;
    while (a < s.size() &&
           std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a &&
           std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool is_key_char(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_';
}

} // namespace

void GuiSettingsEditor::open() {
    if (text_editor::is_active(app.settings_editor)) return;
    text_editor::enter(app.settings_editor,
                       /*target=*/0,
                       /*locked_prefix=*/"",
                       /*initial_pending=*/"",
                       text_editor::Kind::SettingsAssignment);
    viewport.invalidate_timestamp_area();
}

void GuiSettingsEditor::exit_no_commit() {
    if (!text_editor::is_active(app.settings_editor)) return;
    viewport.invalidate_timestamp_area();
    text_editor::deactivate(app.settings_editor);
}

namespace {

// Keys owned by a dedicated gesture; rejected by commit() to keep the
// settings editor as a single canonical entry point for engine keys. This
// covers viewport/playback state, the view toggles, the per-tab read-only
// flags, per-tab trim, and font_size — all are set through their own
// gestures, not here. active_audio_view toggles on bare `t`, active_tab_view
// switches on Ctrl+Tab, the per-tab read-only flags toggle on bare `o`, and
// font_size is a display preference stepped by the Ctrl+Shift+= /
// Ctrl+Shift+- pair (input_handler.cpp).
bool is_view_state_key(const std::string& k) {
    return k == "tab_a_viewport_start"   ||
           k == "tab_a_zoom"             ||
           k == "tab_a_playhead_cursor"  ||
           k == "tab_b_viewport_start"   ||
           k == "tab_b_zoom"             ||
           k == "tab_b_playhead_cursor"  ||
           k == "follow"                 ||
           k == "active_audio_view"      ||
           k == "active_markers_view"    ||
           k == "active_tab_view"        ||
           k == "playback_speed"         ||
           k == "font_size"              ||
           k == "tab_a_trim_begin"       ||
           k == "tab_a_trim_end"         ||
           k == "tab_a_read_only"        ||
           k == "tab_b_trim_begin"       ||
           k == "tab_b_trim_end"         ||
           k == "tab_b_read_only";
}

} // namespace

void GuiSettingsEditor::commit() {
    if (!text_editor::is_active(app.settings_editor)) return;
    const std::string& pending = app.settings_editor.pending;

    // Shape: split on the first `=`. The key is everything before it
    // (validated whitespace-free below, so it never contains `=`); the
    // value is everything after and may itself contain `=`, so a free-text
    // value such as a url= with a `?v=` query parameter or a notes= line
    // commits intact. This matches the settings schema reader
    // (read_settings_file), which also splits on the first `=`.
    const size_t eq = pending.find('=');
    auto reject = [&](const char* reason) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: %s\n", reason);
    };
    if (eq == std::string::npos) { reject("missing '='"); return; }
    const std::string key   = trim_ws(pending.substr(0, eq));
    const std::string value = trim_ws(pending.substr(eq + 1));
    if (key.empty()) { reject("empty key"); return; }
    for (char c : key) {
        if (!is_key_char(c)) { reject("invalid character in key"); return; }
    }

    // 3a. View-state key check. These have dedicated gestures and are
    // not settable through this editor — silently maintaining a parallel
    // pathway here would create two ways to mutate the same thing. This is
    // a deliberate asymmetry with file load: read_settings (settings_io.cpp)
    // accepts these same keys from the .settings file because that file is
    // their persistence domain (view state and trim survive restarts),
    // while the editor is engine-keys-only by design — the two accept sets
    // differ on purpose.
    if (is_view_state_key(key)) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: %s has a dedicated "
            "gesture, not settable here\n", key.c_str());
        return;
    }

    // 3b. Canonical engine-key write. Reject any key that is not in the
    // canonical engine set; validate the value through the same helper
    // the file-load deserializer uses. Capture-before-mutate so the
    // snapshot on the undo stack reflects the pre-edit settings.
    if (!is_canonical_engine_key(key)) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: unknown engine key "
            "'%s'\n", key.c_str());
        return;
    }

    EngineSettings candidate = app.engine_settings;
    std::string reason;
    if (!validate_engine_setting(key, value, candidate, reason)) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: key '%s' has invalid "
            "value '%s': %s\n",
            key.c_str(), value.c_str(), reason.c_str());
        return;
    }

    // Source-clobber guard. The single-render output lands beside the source
    // (the wav deliverable named by title, the map artifacts by the source
    // stem — render_output_stem); an edit that makes any of the format's
    // output paths resolve to the source file itself would overwrite the
    // source on the next Ctrl+Alt+R. The shared predicate composes and checks
    // every path of the format — the warptempo_maps pair's second file is
    // covered by the same refusal. Refuse it here so the colliding value never
    // reaches app.engine_settings.
    if (render_output_source_collision(candidate, app.source_audio_path)) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: this would make the "
            "render output overwrite the source file (%s); choose a "
            "different title or output_format\n",
            std::filesystem::path(app.source_audio_path)
                .filename().string().c_str());
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
            "warptempo_gui: setting unchanged: %s=%s\n",
            key.c_str(), cur_serialized->c_str());
        viewport.invalidate_timestamp_area();
        text_editor::deactivate(app.settings_editor);
        return;
    }

    SettingsSnapshot pre = capture_current_settings(app);
    app.engine_settings = std::move(candidate);
    undo.push_settings_undo(std::move(pre));
    if (key == "output_format" && !target_render.target_view_available()) {
        target_render.leave_target_view();
    }

    std::fprintf(stderr,
        "warptempo_gui: setting applied: %s=%s\n",
        key.c_str(), value.c_str());

    viewport.invalidate_timestamp_area();
    text_editor::deactivate(app.settings_editor);
    // Engine settings are engine input — fire target render.
    // Non-wav output_format leaves target view above, so trigger() marks the
    // buffer dirty but does not dispatch until target view is available again.
    target_render.trigger();
}

void GuiSettingsEditor::autocomplete_value() {
    if (!text_editor::is_active(app.settings_editor)) return;
    const std::string pending = app.settings_editor.pending;

    const size_t eq = pending.find('=');
    if (eq == std::string::npos) return;  // no `key=` yet; nothing to complete

    // Only fill an empty value side, so an in-progress value is never
    // overwritten. Whitespace-only counts as empty.
    if (!trim_ws(pending.substr(eq + 1)).empty()) return;

    const std::string key = trim_ws(pending.substr(0, eq));
    std::optional<std::string> cur =
        format_engine_setting_value(app.engine_settings, key);
    if (!cur) return;  // not a canonical engine key (trim / view-state /
                       // font_size / unknown are not recallable — font_size's
                       // commit route now red-flashes, so recalling it would
                       // be a trap)

    // Rebuild as `<prefix>=<current value>`, cap-aware, cursor at end. The
    // prefix is the typed text up to and including the first `=`, kept
    // verbatim; replace_selection fills the value at the cursor.
    app.settings_editor.pending          = pending.substr(0, eq + 1);
    app.settings_editor.cursor_pos       =
        static_cast<int>(app.settings_editor.pending.size());
    app.settings_editor.selection_anchor = -1;
    text_editor::replace_selection(app.settings_editor, *cur);

    viewport.invalidate_timestamp_area();
}
