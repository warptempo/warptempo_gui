#include "settings_file.h"

#include "frame_format.h"
#include "parse_text_util.h"
#include "playback_speed_presets.h"

#include <cctype>
#include <expected>
#include <fstream>
#include <set>
#include <string>

namespace {

using warptempo_parse::parse_bool_token;
using warptempo_parse::parse_double_strict;
using warptempo_parse::parse_float_strict;
using warptempo_parse::parse_int64_strict;
using warptempo_parse::parse_int_strict;
using warptempo_parse::prefix_line_error;
using warptempo_parse::trim_ws;

}  // namespace

namespace warptempo_settings {

std::unexpected<std::string> bad_value(int ln, const std::string& key,
                                       const std::string& value,
                                       const std::string& rule) {
    return prefix_line_error(
        ln, "key '" + key + "' has invalid value '" + value + "': " + rule);
}

std::expected<void, std::string> scan_settings_file(
        std::istream& in, const SettingsLineFn& on_pair) {
    std::set<std::string> seen;
    std::string line;
    int ln = 0;
    while (std::getline(in, line)) {
        ++ln;
        if (ln == 1) warptempo_parse::strip_bom(line);
        const std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            return prefix_line_error(ln, "not a key=value line");
        }
        const std::string key   = trim_ws(trimmed.substr(0, eq));
        const std::string value = trim_ws(trimmed.substr(eq + 1));
        if (key.empty()) {
            return prefix_line_error(ln, "empty key");
        }
        if (!seen.insert(key).second) {
            return prefix_line_error(ln, "duplicate key '" + key + "'");
        }
        auto r = on_pair(ln, key, value);
        if (!r) return std::unexpected(std::move(r.error()));
    }

    // The getline loop ends on eofbit (normal end of file) or on badbit (a
    // stream read failure mid-file). eofbit+failbit is the ordinary end of a
    // healthy file and parses on; badbit alone is a filesystem or media read
    // error, checked here BEFORE the required-key tail so a read that failed
    // after the three required keys already arrived can never be laundered
    // into a complete-looking file.
    if (in.bad()) {
        return std::unexpected(std::string("I/O read error"));
    }

    for (const char* k : {"title", "scale"}) {
        if (seen.count(k) == 0) {
            return std::unexpected(
                std::string("missing required key '") + k + "'");
        }
    }
    return {};
}

std::optional<std::expected<void, std::string>> try_engine_key(
        int ln, const std::string& key, const std::string& value,
        EngineSettings& engine) {
    if (!is_canonical_engine_key(key)) return std::nullopt;
    // Engine keys route through the single per-(key, value) validator
    // shared with the editor-commit boundary.
    std::string reason;
    if (!validate_engine_setting(key, value, engine, reason)) {
        return std::expected<void, std::string>(bad_value(ln, key, value, reason));
    }
    return std::expected<void, std::string>{};
}

}  // namespace warptempo_settings

std::expected<SettingsFile, std::string> read_settings_file(
        const std::string& path) {
    SettingsFile out;

    std::ifstream f(path);
    if (!f) {
        return std::unexpected("could not open '" + path + "'");
    }

    auto scan = warptempo_settings::scan_settings_file(
        f, [&out](int ln, const std::string& key,
                  const std::string& value)
                  -> std::expected<void, std::string> {
        using warptempo_settings::bad_value;

        if (auto e = warptempo_settings::try_engine_key(ln, key, value,
                                                        out.engine)) {
            return *e;
        }

        // A per-tab key prefix selects the tab; the suffix selects the
        // field. Keeping one arm per field (rather than per tab) keeps the
        // two tabs' grammars identical by construction.
        SettingsFileTab* tab = nullptr;
        std::string suffix;
        if (key.rfind("tab_a_", 0) == 0) {
            tab = &out.tab_a;
            suffix = key.substr(6);
        } else if (key.rfind("tab_b_", 0) == 0) {
            tab = &out.tab_b;
            suffix = key.substr(6);
        }

        if (tab != nullptr && suffix == "viewport_start") {
            int64_t v = 0;
            if (!parse_int64_strict(value, v) || v < 0) {
                return bad_value(ln, key, value,
                                 "must be a non-negative integer");
            }
            tab->has_viewport_start = true;
            tab->viewport_start = v;
        } else if (tab != nullptr && suffix == "zoom") {
            int v = 0;
            if (!parse_int_strict(value, v) ||
                v < kFitFileLevel || v > kMaxNumericLevel) {
                return bad_value(ln, key, value, "must be a zoom level");
            }
            tab->has_zoom = true;
            tab->zoom = v;
        } else if (tab != nullptr && suffix == "playhead_cursor") {
            int64_t v = 0;
            if (!parse_int64_strict(value, v) || v < 0) {
                return bad_value(ln, key, value,
                                 "must be a non-negative integer");
            }
            tab->has_playhead = true;
            tab->playhead = v;
        } else if (tab != nullptr && suffix == "read_only") {
            bool v = false;
            if (!parse_bool_token(value, v)) {
                return bad_value(
                    ln, key, value,
                    "must be one of {true, false, 1, 0, yes, no, on, off}");
            }
            tab->has_read_only = true;
            tab->read_only = v;
        } else if (tab != nullptr && suffix == "trim_begin") {
            int64_t v = 0;
            if (!parse_authored_frame(value, v)) {
                return bad_value(ln, key, value,
                                 "must be a whole source-frame position");
            }
            tab->trim.has_begin = true;
            tab->trim.begin_frame = v;
        } else if (tab != nullptr && suffix == "trim_end") {
            int64_t v = 0;
            if (!parse_authored_frame(value, v)) {
                return bad_value(ln, key, value,
                                 "must be a whole source-frame position");
            }
            tab->trim.has_end = true;
            tab->trim.end_frame = v;
        } else if (key == "follow") {
            // Historical grammar: case-insensitive true/false only.
            std::string lower = value;
            for (char& c : lower) c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
            if (lower == "true")       { out.follow = true;  }
            else if (lower == "false") { out.follow = false; }
            else {
                return bad_value(ln, key, value, "must be true or false");
            }
            out.has_follow = true;
        } else if (key == "active_audio_view") {
            if (value != "S" && value != "T") {
                return bad_value(ln, key, value, "must be S or T");
            }
            out.has_active_audio_view = true;
            out.active_audio_view = value[0];
        } else if (key == "active_markers_view") {
            if (value != "W" && value != "P") {
                return bad_value(ln, key, value, "must be W or P");
            }
            out.has_active_markers_view = true;
            out.active_markers_view = value[0];
        } else if (key == "active_tab_view") {
            if (value != "A" && value != "B") {
                return bad_value(ln, key, value, "must be A or B");
            }
            out.has_active_tab_view = true;
            out.active_tab_view = value[0];
        } else if (key == "playback_speed") {
            // Preset-vocabulary-only on disk: the GUI authors playback_speed
            // exclusively through the Shift+0..9 presets
            // (kPlaybackSpeedPresets, the shared source of truth), so any
            // off-preset value is a state the GUI can never produce.
            // Exact float equality against the table is sound: the writer
            // emits each preset with the same one-decimal %.1f this parses
            // back, and both the on-disk text and the table literal are the
            // nearest float of the same short decimal.
            float v = 0.0f;
            if (!parse_float_strict(value, v) ||
                !is_playback_speed_preset(v)) {
                return bad_value(ln, key, value, "must be a preset speed");
            }
            out.has_playback_speed = true;
            out.playback_speed = v;
        } else if (key == "font_size") {
            double v = 0.0;
            if (!parse_double_strict(value, v) || v < 6.0 || v > 72.0) {
                return bad_value(ln, key, value,
                                 "must be a number in [6, 72]");
            }
            out.has_font_size = true;
            out.font_size = v;
        } else if (key == "audio_player") {
            // GUI-kind launcher for the `l` render-listen command: an external
            // player binary name or path. Any value is accepted (no path/binary
            // grammar — it is user-supplied), INCLUDING empty: an empty value is
            // legal and means "no external player". The key is always present in
            // a product-written .settings, so this reader (the shared GUI+CLI
            // schema) must load `audio_player=` in both products.
            out.has_audio_player = true;
            out.audio_player = value;
        } else {
            return prefix_line_error(ln, "unknown key '" + key + "'");
        }
        return {};
    });
    if (!scan) return std::unexpected(std::move(scan.error()));
    return out;
}
