#include "settings_editor.h"

#include "input_handler.h"
#include "render_output_naming.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "target_render.h"
#include "text_editor.h"
#include "undo.h"

#include "frame_format.h"
#include "parse_text_util.h"
#include "playback_speed_presets.h"
#include "settings_file.h"     // kFitFileLevel .. kMaxNumericLevel

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <optional>
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

// GUI-kind key router. Each arm parses strictly, red-flashes on any malformed
// or out-of-vocabulary value (mirroring the load schema in
// src/parser/settings_file.cpp), and otherwise applies through the key's own
// gesture chokepoint — no parallel state writer. GUI-kind commits touch no
// undo history and no dirty state (launch/view state, like audio_player); a
// same-value commit no-op-deactivates like the engine no-op gate. Playback is
// already stopped (the editor is modal), so the appliers need no playback
// special-casing.
bool GuiSettingsEditor::commit_gui_setting(const std::string& key,
                                           const std::string& value) {
    auto reject = [&](const char* reason) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: %s\n", reason);
    };
    auto applied = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: setting applied: %s=%s\n",
            key.c_str(), value.c_str());
        viewport.invalidate_timestamp_area();
        text_editor::deactivate(app.settings_editor);
    };
    auto unchanged = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: setting unchanged: %s=%s\n",
            key.c_str(), value.c_str());
        viewport.invalidate_timestamp_area();
        text_editor::deactivate(app.settings_editor);
    };

    // -- non-tab GUI keys ------------------------------------------------
    if (key == "playback_speed") {
        float v = 0.0f;
        if (!warptempo_parse::parse_float_strict(value, v) ||
            !is_playback_speed_preset(v)) {
            reject("playback_speed must be a preset speed"); return true;
        }
        if (v == app.playback_speed) { unchanged(); return true; }
        // Stores always; silent-inaudible in target view (the ruled behavior,
        // not a rejection). The one path that writes app.playback_speed.
        playback_lifecycle.set_playback_speed(v);
        applied(); return true;
    }
    if (key == "follow") {
        bool v = false;
        if (!warptempo_parse::parse_bool_token(value, v)) {
            reject("follow must be one of {true, false, 1, 0, yes, no, on, off}");
            return true;
        }
        if (v == app.follow_mode) { unchanged(); return true; }
        playback_lifecycle.set_follow_mode(v);
        applied(); return true;
    }
    if (key == "font_size") {
        double v = 0.0;
        if (!warptempo_parse::parse_double_strict(value, v) ||
            v < 6.0 || v > 72.0) {
            reject("font_size must be a number in [6, 72]"); return true;
        }
        if (v == app.font_size) { unchanged(); return true; }
        input->apply_font_size(v);
        applied(); return true;
    }
    if (key == "active_audio_view") {
        if (value != "S" && value != "T") {
            reject("active_audio_view must be S or T"); return true;
        }
        if (value[0] == app.active_audio_view) { unchanged(); return true; }
        // The bare-`t` route (no editor-state guard); it flips S<->T.
        input->handle_active_audio_view_toggle();
        applied(); return true;
    }
    if (key == "active_markers_view") {
        if (value != "W" && value != "P") {
            reject("active_markers_view must be W or P"); return true;
        }
        if (value[0] == app.active_markers_view) { unchanged(); return true; }
        // The bare-`p` route; it flips W<->P and repaints.
        active_views.toggle_active_markers_view();
        applied(); return true;
    }
    if (key == "active_tab_view") {
        if (value != "A" && value != "B") {
            reject("active_tab_view must be A or B"); return true;
        }
        if (value[0] == app.active_tab_view) { unchanged(); return true; }
        // Exactly the Ctrl+Tab pair.
        active_views.switch_active_tab_view_to(value[0]);
        target_render.trigger();
        applied(); return true;
    }

    // -- per-tab GUI keys ------------------------------------------------
    char tab_char = 0;
    std::string suffix;
    if (key.rfind("tab_a_", 0) == 0) { tab_char = 'A'; suffix = key.substr(6); }
    else if (key.rfind("tab_b_", 0) == 0) { tab_char = 'B'; suffix = key.substr(6); }
    if (tab_char == 0) return false;   // not a GUI-kind key

    const bool active = (tab_char == app.active_tab_view);
    // The tab's band. active_view_state(app) returns exactly this band when the
    // tab is active; read_only lives here (never mirrored to a live field), so
    // the band is authoritative for read_only in both tabs.
    ViewState& band = (tab_char == 'B') ? app.tab_b : app.tab_a;

    if (suffix == "viewport_start") {
        int64_t v = 0;
        if (!warptempo_parse::parse_int64_strict(value, v) || v < 0) {
            reject("viewport_start must be a non-negative integer"); return true;
        }
        if (active) {
            if (v == app.viewport_start_sample) { unchanged(); return true; }
            // Assign-then-clamp: the same idiom every viewport mutation uses;
            // the clamp owns out-of-range constructively.
            app.viewport_start_sample = v;
            clamp_viewport_start(app, audio);
            viewport.invalidate_waveform_area();
            viewport.kick_waveform_sync();
        } else {
            if (v == band.viewport_start_sample) { unchanged(); return true; }
            band.viewport_start_sample = v;   // the restore clamps at tab-in
        }
        applied(); return true;
    }
    if (suffix == "zoom") {
        int v = 0;
        if (!warptempo_parse::parse_int_strict(value, v) ||
            v < kFitFileLevel || v > kMaxNumericLevel) {
            reject("zoom must be a zoom level"); return true;
        }
        if (active) {
            if (v == app.zoom_level) { unchanged(); return true; }
            viewport.apply_zoom_change(v);
        } else {
            if (v == band.zoom_level) { unchanged(); return true; }
            band.zoom_level = v;
        }
        applied(); return true;
    }
    if (suffix == "playhead_cursor") {
        int64_t v = 0;
        if (!warptempo_parse::parse_int64_strict(value, v) || v < 0) {
            reject("playhead_cursor must be a non-negative integer"); return true;
        }
        if (active) {
            if (v == app.playhead_cursor_sample) { unchanged(); return true; }
            // The live chokepoint; its clamp owns out-of-range constructively.
            viewport.move_playhead_to(v);
        } else {
            if (v == band.playhead_cursor_sample) { unchanged(); return true; }
            band.playhead_cursor_sample = v;
        }
        applied(); return true;
    }
    if (suffix == "read_only") {
        bool v = false;
        if (!warptempo_parse::parse_bool_token(value, v)) {
            reject("read_only must be one of {true, false, 1, 0, yes, no, on, off}");
            return true;
        }
        // Navigation-class (allowed even while the tab is read-only). The
        // editor cannot OPEN in a read-only tab (its `:` opener drops at the
        // read-only key gate), so this is also the remote-unlock route for the
        // OTHER tab. read_only lives in the band for both tabs.
        if (v == band.read_only) { unchanged(); return true; }
        band.read_only = v;
        applied(); return true;
    }
    if (suffix == "trim_begin" || suffix == "trim_end") {
        const bool is_begin = (suffix == "trim_begin");
        // Trim is an authoring mutation: its gestures refuse in a read-only
        // tab, so mirror that here (viewport / zoom / playhead / read_only
        // above are navigation-class and stay allowed).
        if (band.read_only) {
            reject("tab is read-only; trim is not settable here"); return true;
        }
        // An empty value clears that bound (the key's absent-in-file form).
        if (value.empty()) {
            TrimState& t = active ? app.trim : band.trim;
            bool& has = is_begin ? t.has_begin : t.has_end;
            if (!has) { unchanged(); return true; }
            int64_t& fr = is_begin ? t.begin_frame : t.end_frame;
            bool& sel = is_begin
                ? (active ? app.trim_begin_selected : band.trim_begin_selected)
                : (active ? app.trim_end_selected   : band.trim_end_selected);
            has = false;
            fr = 0;
            sel = false;   // an unset bound can't stay selected
            if (active) {
                viewport.invalidate_waveform_area();
                target_render.trigger();
            }
            applied(); return true;
        }
        int64_t v = 0;
        if (!parse_authored_frame(value, v)) {
            reject("trim bound must be a whole source-frame position");
            return true;
        }
        // Per-bound walls, exactly the load guard's compare: begin 0..EOF-1,
        // end 0..EOF.
        const int64_t total = audio.total_frames();
        const int64_t wall = is_begin ? total - 1 : total;
        if (v < 0 || v > wall) {
            reject("trim bound is past its wall"); return true;
        }
        TrimState& t = active ? app.trim : band.trim;
        bool& has = is_begin ? t.has_begin : t.has_end;
        int64_t& fr = is_begin ? t.begin_frame : t.end_frame;
        if (has && fr == v) { unchanged(); return true; }
        has = true;
        fr = v;
        if (active) {
            // The same commit tail trim gestures use, including
            // auto_clear_crossed_trim (a bound committed onto/across its
            // partner destroys both, silently). History-less, like all trim.
            input->auto_clear_crossed_trim();
            viewport.invalidate_waveform_area();
            target_render.trigger();
        } else if (t.has_begin && t.has_end && t.end_frame <= t.begin_frame) {
            // Inactive band: the load convention — a crossed/equal resulting
            // pair clears both bounds, one stderr line.
            t.has_begin = false;
            t.has_end = false;
            t.begin_frame = 0;
            t.end_frame = 0;
            band.trim_begin_selected = false;
            band.trim_end_selected = false;
            band.last_selected_trim = 0;
            std::fprintf(stderr,
                "warptempo_gui: tab_%c trim crossed (end <= begin); both "
                "bounds cleared\n", (tab_char == 'B') ? 'b' : 'a');
        }
        applied(); return true;
    }

    // A tab_a_/tab_b_ prefix with an unrecognized suffix is not a GUI-kind
    // key; fall through so the engine path reports "unknown engine key".
    return false;
}

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

    // audio_player is the one GUI-kind key with no dedicated gesture at all —
    // a free-text launcher path, so the settings editor is its sole authoring
    // surface. Handled here rather than through commit_gui_setting (which
    // routes every OTHER GUI key into its gesture chokepoint). Set it directly
    // (an empty value means no external player — the writer always emits the
    // line as `audio_player=`, which re-loads as no-player). A launch
    // preference like font_size: no undo history, no dirty tracking, silently
    // persisted on Ctrl+S.
    if (key == "audio_player") {
        app.audio_player = value;
        std::fprintf(stderr, "warptempo_gui: audio_player set: '%s'\n",
            value.c_str());
        viewport.invalidate_timestamp_area();
        text_editor::deactivate(app.settings_editor);
        return;
    }

    // 3a. GUI-kind keys. Every key that can appear in a `.settings` file is
    // settable here: the router parses strictly and applies through the key's
    // own gesture chokepoint (no parallel writer). It returns true when it has
    // fully handled the commit (applied + deactivated, or red-flashed); false
    // when `key` is not a GUI-kind key, so we fall through to the engine path.
    if (commit_gui_setting(key, value)) return;

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

    // Source-clobber guard. The single-render wav output lands beside the
    // source, named by title (render_output_stem); an edit that makes the
    // output path resolve to the source file itself would overwrite the
    // source on the next Ctrl+Alt+R. The shared predicate composes and checks
    // that path. Refuse it here so the colliding value never reaches
    // app.engine_settings.
    if (render_output_source_collision(candidate, app.source_audio_path)) {
        app.settings_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: settings edit rejected: this would make the "
            "render output overwrite the source file (%s); choose a "
            "different title\n",
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

    std::fprintf(stderr,
        "warptempo_gui: setting applied: %s=%s\n",
        key.c_str(), value.c_str());

    viewport.invalidate_timestamp_area();
    text_editor::deactivate(app.settings_editor);
    // Engine settings are engine input — fire target render.
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
    // Recall the current live value for ANY settable key. Engine keys read
    // through format_engine_setting_value; GUI-kind keys (view state,
    // playback_speed, follow, font_size, audio_player, per-tab trim / read_only)
    // read through recall_gui_setting_value — which produces byte-identical
    // output to what a Ctrl+S would write, so recall and save never diverge.
    // An unset optional trim recalls as the empty value (`tab_a_trim_begin=`).
    // Only a truly unknown key is not recallable.
    std::optional<std::string> cur =
        format_engine_setting_value(app.engine_settings, key);
    if (!cur) cur = recall_gui_setting_value(app, key);
    if (!cur) return;  // unknown key: nothing to recall

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
