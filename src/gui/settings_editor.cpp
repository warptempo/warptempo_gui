#include "settings_editor.h"

#include "settings_io.h"
#include "target_render.h"
#include "text_editor.h"
#include "time_format.h"
#include "undo.h"

#include <cctype>
#include <cstdio>
#include <string>
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

// Keys with a dedicated authoring gesture; rejected by commit() to keep
// the settings editor as a single canonical entry point for everything
// else. The per-tab trim keys are rejected because trim is also typed
// via the canonical `trim_begin` / `trim_end` keys (which route to the
// active tab); exposing the per-tab spelling here would be a parallel
// pathway with the same content.
bool is_view_state_key(const std::string& k) {
    return k == "tab_a_viewport_start" ||
           k == "tab_a_zoom"           ||
           k == "tab_a_playhead"       ||
           k == "tab_b_viewport_start" ||
           k == "tab_b_zoom"           ||
           k == "tab_b_playhead"       ||
           k == "follow"               ||
           k == "active_markers_view"          ||
           k == "playback_speed"       ||
           k == "tab_a_trim_begin"     ||
           k == "tab_a_trim_end"       ||
           k == "tab_b_trim_begin"     ||
           k == "tab_b_trim_end";
}

} // namespace

void GuiSettingsEditor::commit() {
    if (!text_editor::is_active(app.settings_editor)) return;
    const std::string& pending = app.settings_editor.pending;

    // Strict shape: exactly one `=`, non-empty whitespace-free key.
    const size_t eq = pending.find('=');
    auto reject = [&](const char* reason) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: %s\n", reason);
    };
    if (eq == std::string::npos) { reject("missing '='"); return; }
    if (pending.find('=', eq + 1) != std::string::npos) {
        reject("more than one '='"); return;
    }
    const std::string key   = trim_ws(pending.substr(0, eq));
    const std::string value = trim_ws(pending.substr(eq + 1));
    if (key.empty()) { reject("empty key"); return; }
    for (char c : key) {
        if (!is_key_char(c)) { reject("invalid character in key"); return; }
    }

    // 3a. View-state key check. These have dedicated gestures and are
    // not settable through this editor — silently maintaining a parallel
    // pathway here would create two ways to mutate the same thing.
    if (is_view_state_key(key)) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: %s has a dedicated "
            "gesture, not settable here\n", key.c_str());
        return;
    }

    // 3b. Typed-field trim keys: parse MM:SS.mmm, write to the active
    // tab, push a settings-undo entry. The canonical spelling
    // (trim_begin / trim_end without a tab prefix) always routes to the
    // active tab; the per-tab spellings hit the view-state rejection
    // above.
    if (key == "trim_begin" || key == "trim_end") {
        if (!is_settings_timestamp(value)) {
            app.settings_editor.red = true;
            viewport.invalidate_timestamp_area();
            std::fprintf(stderr,
                "warptempo_gui: settings edit rejected: %s expects "
                "MM:SS.mmm, got %s\n", key.c_str(), value.c_str());
            return;
        }
        const double secs = parse_timestamp(value);
        SettingsSnapshot pre = capture_current_settings(app);
        ViewState& vs = active_view_state(app);
        if (key == "trim_begin") {
            vs.has_trim_begin     = true;
            vs.trim_begin_seconds = secs;
        } else {
            vs.has_trim_end     = true;
            vs.trim_end_seconds = secs;
        }
        undo.push_settings_undo(std::move(pre));
        std::fprintf(stderr,
            "warptempo_gui: setting applied: %s=%s\n",
            key.c_str(), value.c_str());
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        text_editor::deactivate(app.settings_editor);
        // Trim is engine input — fire the target render.
        target_render.trigger();
        return;
    }

    // 3c. Canonical engine-key write. Reject any key that is not in the
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

    SettingsSnapshot pre = capture_current_settings(app);
    app.engine_settings = std::move(candidate);
    undo.push_settings_undo(std::move(pre));

    std::fprintf(stderr,
        "warptempo_gui: setting applied: %s=%s\n",
        key.c_str(), value.c_str());

    viewport.invalidate_timestamp_area();
    text_editor::deactivate(app.settings_editor);
    // Engine settings are engine input — fire target render.
    // (title / output_format don't change rendered audio in any
    // user-visible way, but the trigger is cheap and the target render
    // surfaces in target view.)
    target_render.trigger();
}
