#include "settings_editor.h"

#include "text_editor.h"

#include <cctype>
#include <cstdio>
#include <string>

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

    // Apply: overwrite or append in settings_passthrough. Typed-field
    // keys (tab_a_viewport_start, follow, active_mode, etc.) bypass
    // their owning fields in this brief — the next brief that adds
    // settings-undo will route them properly.
    bool replaced = false;
    for (auto& kv : app.settings_passthrough) {
        if (kv.first == key) {
            kv.second = value;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        app.settings_passthrough.emplace_back(key, value);
    }

    std::fprintf(stderr,
        "warptempo_gui: setting applied: %s=%s\n",
        key.c_str(), value.c_str());

    // engine= boot-out: P mode requires engine=warptempo (see
    // GuiTabMode::toggle_active_mode). Setting any other engine while
    // in P drops the user back to W so the mode invariant holds.
    if (key == "engine" && value != "warptempo" &&
        app.active_mode == 'P') {
        tab_mode.switch_active_mode_to('W');
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: phase_reset mode disabled: engine=%s\n",
            value.c_str());
    }

    viewport.invalidate_timestamp_area();
    text_editor::deactivate(app.settings_editor);
}
