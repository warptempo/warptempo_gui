#include "settings_editor.h"

#include "render_pipeline.h"
#include "settings_io.h"
#include "target_render.h"
#include "text_editor.h"
#include "time_format.h"
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

// Keys with a dedicated authoring gesture; rejected by commit() to keep
// the settings editor as a single canonical entry point for everything
// else. Trim is handled separately below via the canonical `trim_begin` /
// `trim_end` keys (which route to the project trim), so it is not listed
// here.
bool is_view_state_key(const std::string& k) {
    return k == "tab_a_viewport_start"   ||
           k == "tab_a_zoom"             ||
           k == "tab_a_playhead_cursor"  ||
           k == "tab_b_viewport_start"   ||
           k == "tab_b_zoom"             ||
           k == "tab_b_playhead_cursor"  ||
           k == "follow"                 ||
           k == "active_markers_view"    ||
           k == "playback_speed";
}

} // namespace

void GuiSettingsEditor::commit() {
    if (!text_editor::is_active(app.settings_editor)) return;
    const std::string& pending = app.settings_editor.pending;

    // Shape: split on the first `=`. The key is everything before it
    // (validated whitespace-free below, so it never contains `=`); the
    // value is everything after and may itself contain `=`, so a free-text
    // value such as a url= with a `?v=` query parameter or a notes= line
    // commits intact. This matches read_engine_settings_from_file, which
    // also splits on the first `=`.
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
    // pathway here would create two ways to mutate the same thing.
    if (is_view_state_key(key)) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: %s has a dedicated "
            "gesture, not settable here\n", key.c_str());
        return;
    }

    // 3b. Typed-field trim keys: parse MM:SS.mmm, write the project trim,
    // push a settings-undo entry. The canonical spelling (trim_begin /
    // trim_end) is the only trim key; there is no per-tab spelling.
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
        if (key == "trim_begin") {
            app.has_trim_begin     = true;
            app.trim_begin_seconds = secs;
        } else {
            app.has_trim_end     = true;
            app.trim_end_seconds = secs;
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

    // Source-clobber guard. The single-render output lands beside the
    // source as <source_dir>/<title><ext>; an edit that makes that path
    // resolve to the source file itself would overwrite the source on the
    // next Ctrl+Alt+R. Refuse it here so the colliding value never reaches
    // app.engine_settings.
    if (!app.source_audio_path.empty()) {
        const std::filesystem::path out =
            compose_sibling_output_path(app.source_audio_path, candidate);
        const std::filesystem::path src(app.source_audio_path);
        std::error_code ec;
        const bool same =
            std::filesystem::equivalent(out, src, ec)
            || out.lexically_normal() == src.lexically_normal();
        if (same) {
            app.settings_editor.red = true;
            viewport.invalidate_timestamp_area();
            std::fprintf(stderr,
                "warptempo_gui: settings edit rejected: this would make the "
                "render output overwrite the source file (%s); choose a "
                "different title or output_format\n",
                src.filename().string().c_str());
            return;
        }
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

void GuiSettingsEditor::autocomplete_value() {
    if (!text_editor::is_active(app.settings_editor)) return;
    const std::string pending = app.settings_editor.pending;

    const size_t eq = pending.find('=');
    if (eq == std::string::npos) return;  // no `key=` yet; nothing to complete

    // Only fill an empty value side, so an in-progress value is never
    // overwritten. Whitespace-only counts as empty.
    if (!trim_ws(pending.substr(eq + 1)).empty()) return;

    const std::string key = trim_ws(pending.substr(0, eq));
    const std::optional<std::string> cur =
        format_engine_setting_value(app.engine_settings, key);
    if (!cur) return;  // not a canonical engine key (trim / view-state / unknown)

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
