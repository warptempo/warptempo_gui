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

// Every canonical .settings key, in kSettingsOrder's on-disk order. This is
// the membership SoT for the required-key check: EVERY key is required, so a
// file missing any one is load-fatal. It is the parser-side twin of the GUI
// writer's kSettingsOrder (settings_io.cpp) — adding a settings key touches
// BOTH lists (and, for its grammar, kEngineKeys/validate_engine_setting for an
// engine key or validate_gui_setting for a GUI-kind key). kEngineKeys stays
// file-local to engine_settings_io.cpp; this flat list owns membership
// completeness here.
constexpr const char* kCanonicalSettingsKeys[] = {
    "title", "scale", "bpm", "notes", "url", "cover",
    "active_audio_view", "active_markers_view", "active_tab_view",
    "playback_speed", "follow", "font_size", "audio_player",
    "tab_a_trim_begin", "tab_a_trim_end", "tab_a_read_only",
    "tab_a_viewport_start", "tab_a_zoom", "tab_a_playhead_cursor",
    "tab_b_trim_begin", "tab_b_trim_end", "tab_b_read_only",
    "tab_b_viewport_start", "tab_b_zoom", "tab_b_playhead_cursor",
};

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

    for (const char* k : kCanonicalSettingsKeys) {
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

std::optional<std::expected<GuiSettingValue, std::string>> validate_gui_setting(
        const std::string& key, const std::string& value) {
    using R = std::expected<GuiSettingValue, std::string>;
    auto err = [](std::string reason) -> R {
        return std::unexpected(std::move(reason));
    };

    GuiSettingValue out;

    // A per-tab key prefix selects the tab (routing is the caller's job); the
    // suffix selects the field. The two tabs share one grammar by construction.
    if (key.rfind("tab_a_", 0) == 0 || key.rfind("tab_b_", 0) == 0) {
        const std::string suffix = key.substr(6);
        if (suffix == "viewport_start" || suffix == "playhead_cursor") {
            int64_t v = 0;
            if (!parse_int64_strict(value, v) || v < 0)
                return err("must be a non-negative integer");
            out.i64 = v;
            return R(out);
        }
        if (suffix == "zoom") {
            int v = 0;
            if (!parse_int_strict(value, v) ||
                v < kFitFileLevel || v > kMaxNumericLevel)
                return err("must be a zoom level");
            out.i = v;
            return R(out);
        }
        if (suffix == "read_only") {
            bool v = false;
            if (!parse_bool_token(value, v))
                return err(
                    "must be one of {true, false, 1, 0, yes, no, on, off}");
            out.b = v;
            return R(out);
        }
        if (suffix == "trim_begin" || suffix == "trim_end") {
            // The unset spelling is the exact literal `-1`. It sits outside the
            // authored-frame domain (parse_authored_frame refuses a sign), so
            // the value grammar is unambiguous: `-1` (unset) or a canonical
            // whole source frame. The past-EOF wall stays state-dependent
            // (caller-side).
            if (value == "-1") {
                out.trim_unset = true;
                return R(out);
            }
            int64_t v = 0;
            if (!parse_authored_frame(value, v))
                return err("must be a whole source-frame position or -1 (unset)");
            out.i64 = v;
            return R(out);
        }
        return std::nullopt;  // unrecognized tab suffix: not a GUI-kind key
    }

    if (key == "follow") {
        // Bools share parse_bool_token schema-wide (same grammar as the
        // per-tab read_only keys).
        bool v = false;
        if (!parse_bool_token(value, v))
            return err("must be one of {true, false, 1, 0, yes, no, on, off}");
        out.b = v;
        return R(out);
    }
    if (key == "active_audio_view") {
        if (value != "S" && value != "T") return err("must be S or T");
        out.c = value[0];
        return R(out);
    }
    if (key == "active_markers_view") {
        if (value != "W" && value != "P") return err("must be W or P");
        out.c = value[0];
        return R(out);
    }
    if (key == "active_tab_view") {
        if (value != "A" && value != "B") return err("must be A or B");
        out.c = value[0];
        return R(out);
    }
    if (key == "playback_speed") {
        // Preset-vocabulary-only on disk: the GUI authors playback_speed
        // through the settings editor (:playback_speed=), whose commit
        // red-flashes any value outside kPlaybackSpeedPresets (the shared
        // source of truth), so any off-preset value is a state the GUI can
        // never produce. Exact float equality against the table is sound: the
        // writer emits each preset with the same one-decimal %.1f this parses
        // back, and both the on-disk text and the table literal are the
        // nearest float of the same short decimal.
        float v = 0.0f;
        if (!parse_float_strict(value, v) || !is_playback_speed_preset(v))
            return err("must be a preset speed");
        out.f = v;
        return R(out);
    }
    if (key == "font_size") {
        double v = 0.0;
        if (!parse_double_strict(value, v) || v < 6.0 || v > 72.0)
            return err("must be a number in [6, 72]");
        out.d = v;
        return R(out);
    }
    if (key == "audio_player") {
        // GUI-kind launcher for the `l` render-listen command: an external
        // player binary name or path. Any value is accepted (no path/binary
        // grammar — it is user-supplied), INCLUDING empty: an empty value is
        // legal and means "no external player". The key is always present in a
        // product-written .settings, so the shared schema loads `audio_player=`
        // in both products.
        out.text = value;
        return R(out);
    }

    return std::nullopt;  // not a GUI-kind key
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

        // The single GUI-kind grammar owner. std::nullopt means the key is
        // neither an engine key (checked above) nor a GUI-kind key — the
        // unknown-key case; an expected error carries the reason bad_value
        // composes. On success, store the typed value into the SettingsFile
        // fields; tab routing and the trim unset decode stay here (state the
        // schema function is deliberately blind to).
        auto g = warptempo_settings::validate_gui_setting(key, value);
        if (!g) {
            return prefix_line_error(ln, "unknown key '" + key + "'");
        }
        if (!*g) {
            return bad_value(ln, key, value, (*g).error());
        }
        const warptempo_settings::GuiSettingValue& gv = **g;

        SettingsFileTab* tab = nullptr;
        std::string suffix;
        if (key.rfind("tab_a_", 0) == 0) {
            tab = &out.tab_a;
            suffix = key.substr(6);
        } else if (key.rfind("tab_b_", 0) == 0) {
            tab = &out.tab_b;
            suffix = key.substr(6);
        }

        if (tab != nullptr) {
            if (suffix == "viewport_start") {
                tab->viewport_start = gv.i64;
            } else if (suffix == "zoom") {
                tab->zoom = gv.i;
            } else if (suffix == "playhead_cursor") {
                tab->playhead = gv.i64;
            } else if (suffix == "read_only") {
                tab->read_only = gv.b;
            } else if (suffix == "trim_begin") {
                // The `-1` unset spelling leaves has_begin false (SettingsTrim's
                // default); any other value sets the bound.
                if (!gv.trim_unset) {
                    tab->trim.has_begin = true;
                    tab->trim.begin_frame = gv.i64;
                }
            } else if (suffix == "trim_end") {
                if (!gv.trim_unset) {
                    tab->trim.has_end = true;
                    tab->trim.end_frame = gv.i64;
                }
            }
        } else if (key == "follow") {
            out.follow = gv.b;
        } else if (key == "active_audio_view") {
            out.active_audio_view = gv.c;
        } else if (key == "active_markers_view") {
            out.active_markers_view = gv.c;
        } else if (key == "active_tab_view") {
            out.active_tab_view = gv.c;
        } else if (key == "playback_speed") {
            out.playback_speed = gv.f;
        } else if (key == "font_size") {
            out.font_size = gv.d;
        } else if (key == "audio_player") {
            out.audio_player = gv.text;
        }
        return {};
    });
    if (!scan) return std::unexpected(std::move(scan.error()));
    return out;
}
