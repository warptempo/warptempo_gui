#include "device_config.h"

#include "settings_io.h"       // atomic_write_string_to_path
#include "settings_file.h"     // warptempo_settings::scan_key_value_file
#include "frame_format.h"      // parse_authored_frame
#include "value_format.h"      // format_value_double / parse_value_double
#include "parse_text_util.h"   // warptempo_parse::prefix_line_error

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace {

// The file's key set, in on-disk order — the writer's order AND the required
// set the shared scanner enforces after the loop (ELEVEN keys since the
// fourth tuning key became the two level keys 2026-09-25 — the foreground's
// `waveform_magnified_gain_db` and the background's `waveform_ghost_gain_db`,
// in decibels; ten from that key's arrival earlier the same day after the
// colour keys, as the raw bar's core gain `waveform_magnified_gain` and then
// the ghost's `waveform_ghost_reduction`; nine from the three waveform colour
// keys' arrival earlier that day for a tuning phase, after sync_path where the picture keys had stood; six from
// when the eleven
// waveform picture keys — the `waveform_gain_*` tunables appended 2026-09-23,
// the `waveform_expander_*` ones 2026-09-24, as many as seventeen in all —
// left 2026-09-24 with the values hard-coded in waveform_gain.cpp; six from
// `max_waveform_height`'s arrival 2026-09-13 with the waveform cap leaving
// render.h; five from `sync_path`'s arrival 2026-08-30 with the mirror's
// configured destination; four from
// `audio_player`'s retirement 2026-08-28; five from the project model
// 2026-08-27; two before it). THE ORDER IS THE ARCHITECT'S OWN, given with the
// fifth key (2026-08-30): gui_scale, projects_repo, projects_path,
// last_project, sync_path — the sixth placed right after gui_scale
// (architect 2026-09-13), and the three colour keys after sync_path in the
// painter's own order: the dark plate's ink, the lit raw bar's, the ghost's,
// with the lit plate's two levels after them, the foreground's then the
// background's (architect 2026-09-25). The
// scanner takes it as a
// SET: it checks that each key ARRIVED, never that it arrived here, so this
// order is the writer's alone and the reader is order-insensitive (the header's
// schema paragraph owns that ruling). One list, so a key cannot be written and
// not demanded (the `.settings` schema keeps the same discipline across two
// lists because its writer is GUI-side and its reader parser-side;
// here both halves are in this file, so one list is the honest shape).
constexpr const char* kDeviceConfigKeys[] = {
    "gui_scale",
    "max_waveform_height",
    "projects_repo",
    "projects_path",
    "last_project",
    "sync_path",
    "waveform_ink",
    "waveform_magnified_ink",
    "waveform_ghost_ink",
    "waveform_magnified_gain_db",
    "waveform_ghost_gain_db",
};

} // namespace

std::string format_gui_scale_percent(int percent) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", percent);
    return std::string(buf);
}

std::string format_max_waveform_height(int authored_px) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", authored_px);
    return std::string(buf);
}

GuiColor parse_waveform_colour(std::string_view v) {
    // The grammar has admitted exactly `#` and six lower-case hex digits, so
    // every digit maps and the value fits 24 bits.
    uint32_t rgb = 0;
    for (size_t i = 1; i < v.size(); ++i) {
        const char c = v[i];
        const uint32_t d = (c >= '0' && c <= '9')
                               ? static_cast<uint32_t>(c - '0')
                               : static_cast<uint32_t>(c - 'a' + 10);
        rgb = (rgb << 4) | d;
    }
    return hex(rgb);
}

std::string format_waveform_colour(GuiColor c) {
    const auto byte = [](double ch) {
        return static_cast<unsigned>(std::nearbyint(ch * 255.0));
    };
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                  byte(c.r), byte(c.g), byte(c.b));
    return std::string(buf);
}

std::string format_waveform_level_db(double v) {
    // The magnitude through the value road (which spells no sign), the sign
    // re-attached below zero. std::fabs, not a negation, so a negative zero
    // spells `0.00` as the reader demands.
    const std::string mag = format_value_double(std::fabs(v), 2);
    return v < 0.0 ? "-" + mag : mag;
}

bool parse_waveform_level_db(std::string_view s, double& out) {
    // The sidecar `scale` key's shape (validate_engine_setting,
    // engine_settings_io.cpp) at min 2, with the sign handled here because
    // parse_value_double refuses one: strip ONE leading '-', the strict
    // magnitude parse (a second sign refuses there), the round trip back to
    // the bytes read — which refuses `-0.00` (the writer spells zero `0.00`)
    // and every non-canonical spelling — then the range owner.
    const bool negative = !s.empty() && s.front() == '-';
    double mag = 0.0;
    if (!parse_value_double(negative ? s.substr(1) : s, mag)) return false;
    const double v = negative ? -mag : mag;
    if (format_waveform_level_db(v) != s) return false;
    if (!is_waveform_level_db(v)) return false;
    out = v;
    return true;
}

std::filesystem::path device_config_path() {
    // The render cache's resolution shape verbatim (RenderCache::init,
    // render_cache.cpp), one variable over: XDG first, then HOME/.config, then
    // nothing. On Android neither variable exists until the backend sets them,
    // which android_main does before gui_main runs, so this resolver needs no
    // platform arm of its own.
    std::string base;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0]) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && home[0]) {
        base = std::string(home) + "/.config";
    } else {
        return {};
    }
    return std::filesystem::path(base) / "warptempo_gui" / "config";
}

std::string format_device_config_text(const DeviceConfig& cfg) {
    std::string s;
    for (const char* key : kDeviceConfigKeys) {
        s += key;
        s += '=';
        const std::string_view k(key);
        // The arms are in the emitted order above, so this reads as the file
        // reads.
        if (k == "gui_scale") {
            s += format_gui_scale_percent(cfg.gui_scale);
        } else if (k == "max_waveform_height") {
            s += format_max_waveform_height(cfg.max_waveform_height);
        } else if (k == "projects_repo") {
            // Free text, verbatim; blank is legal and never matches a remote.
            s += cfg.projects_repo;
        } else if (k == "projects_path") {
            // Verbatim: the reader accepted it as an absolute path, and
            // nothing in the program rewrites it.
            s += cfg.projects_path;
        } else if (k == "last_project") {
            // The folder name verbatim, blank until the first successful open.
            s += cfg.last_project;
        } else if (k == "sync_path") {
            // Verbatim, and blank on a device with no destination — the
            // reader accepted it as empty or as an absolute path, and nothing
            // in the program rewrites it.
            s += cfg.sync_path;
        } else if (k == "waveform_ink") {
            // The three colour keys through their one serializer, which
            // writes the canonical lower-case spelling the reader demands.
            s += format_waveform_colour(cfg.waveform_ink);
        } else if (k == "waveform_magnified_ink") {
            s += format_waveform_colour(cfg.waveform_magnified_ink);
        } else if (k == "waveform_ghost_ink") {
            s += format_waveform_colour(cfg.waveform_ghost_ink);
        } else if (k == "waveform_magnified_gain_db") {
            // The two levels through their one serializer, the canonical
            // signed min-2-decimal spelling the reader demands back.
            s += format_waveform_level_db(cfg.waveform_magnified_gain_db);
        } else if (k == "waveform_ghost_gain_db") {
            s += format_waveform_level_db(cfg.waveform_ghost_gain_db);
        }
        s += '\n';
    }
    return s;
}

std::expected<DeviceConfig, std::string> read_device_config(
        const std::filesystem::path& path) {
    DeviceConfig out;

    std::ifstream f(path);
    if (!f) {
        return std::unexpected("could not open '" + path.string() + "'");
    }

    auto scan = warptempo_settings::scan_key_value_file(
        f, [&out](int ln, const std::string& key, const std::string& value)
                  -> std::expected<void, std::string> {
        using warptempo_settings::bad_value;

        // The arms are in the writer's order (kDeviceConfigKeys above); the
        // scanner hands them over in the FILE's order, which is nobody's
        // business but the file's.
        if (key == "gui_scale") {
            // One canonical spelling: plain digits through
            // parse_authored_frame (no sign, point, or leading zeros — exactly
            // format_gui_scale_percent's `%d` output), then the RANGE through
            // the one owner in the header.
            int64_t v = 0;
            if (!parse_authored_frame(value, v) || !is_gui_scale_percent(v)) {
                return bad_value(ln, key, value, kGuiScaleGrammarReason);
            }
            // Range-checked above, so the narrowing to int is exact.
            out.gui_scale = static_cast<int>(v);
            return {};
        }
        if (key == "max_waveform_height") {
            // The scale's road exactly: plain digits through
            // parse_authored_frame (so `0` is the one spelling of "no
            // maximum"), then the RANGE through the one owner in the header.
            int64_t v = 0;
            if (!parse_authored_frame(value, v) ||
                !is_max_waveform_height(v)) {
                return bad_value(ln, key, value,
                                 kMaxWaveformHeightGrammarReason);
            }
            out.max_waveform_height = static_cast<int>(v);
            return {};
        }
        if (key == "projects_repo") {
            // Free text, taken verbatim in UTF-8 under the one grammar in the
            // header (is_projects_repo: no line separator, the format's own
            // limit, and nothing about hosts or paths — the GitHub recheck
            // judges the value against the clone's `origin`). An empty value
            // is legal and simply never matches, disabling the feature. (The
            // arm moved here verbatim from the sidecar schema, architect
            // approval 2026-08-27; the predicate joined 2026-09-02 with the
            // settings editor's three device-key arms.)
            if (!is_projects_repo(value)) {
                return bad_value(ln, key, value, kProjectsRepoGrammarReason);
            }
            out.projects_repo = value;
            return {};
        }
        if (key == "projects_path") {
            // Absolute, under the shared path-value grammar — the one owner
            // in the header, whose reason is spelled there too (it names the
            // edge rule, for the reason said beside it); existence is
            // startup's question, not this reader's.
            if (!is_projects_path(value)) {
                return bad_value(ln, key, value, kProjectsPathGrammarReason);
            }
            out.projects_path = value;
            return {};
        }
        if (key == "last_project") {
            // Empty, or one path component — the grammar and the reason a
            // separator is adversarial are at is_last_project_name.
            if (!is_last_project_name(value)) {
                return bad_value(ln, key, value,
                    "must be one folder name, not a path");
            }
            out.last_project = value;
            return {};
        }
        if (key == "sync_path") {
            // Empty, or an absolute path under the shared path grammar — the
            // one owner in the header, where the empty form's meaning ("not
            // set up on this device") is stated.
            if (!is_sync_path(value)) {
                return bad_value(ln, key, value, kSyncPathGrammarReason);
            }
            out.sync_path = value;
            return {};
        }
        if (key == "waveform_ink" || key == "waveform_magnified_ink" ||
            key == "waveform_ghost_ink") {
            // `#` and six lower-case hex digits, one canonical spelling,
            // through the one grammar owner in the header; then the one
            // parser. The three are one shape and differ only in the member.
            if (!is_waveform_colour(value)) {
                return bad_value(ln, key, value, kWaveformColourGrammarReason);
            }
            const GuiColor c = parse_waveform_colour(value);
            if (key == "waveform_ink")                out.waveform_ink = c;
            else if (key == "waveform_magnified_ink") out.waveform_magnified_ink = c;
            else                                      out.waveform_ghost_ink = c;
            return {};
        }
        if (key == "waveform_magnified_gain_db" ||
            key == "waveform_ghost_gain_db") {
            // One canonical signed spelling and the range, both through the
            // one parser in the header (the sign stripped, the settings'
            // bracketed-double road on the magnitude, then
            // is_waveform_level_db). The two are one shape and differ only in
            // the member.
            double v = 0.0;
            if (!parse_waveform_level_db(value, v)) {
                return bad_value(ln, key, value, kWaveformLevelDbGrammarReason);
            }
            if (key == "waveform_magnified_gain_db") out.waveform_magnified_gain_db = v;
            else                                     out.waveform_ghost_gain_db = v;
            return {};
        }
        return warptempo_parse::prefix_line_error(
            ln, "unknown key '" + key + "'");
    }, kDeviceConfigKeys);
    if (!scan) return std::unexpected(std::move(scan.error()));
    return out;
}

std::optional<GuiFailure> write_device_config(const DeviceConfig& cfg) {
    // THE TWO CLAUSES ARE COMPOSED HERE (GuiFailure, failure.h — 2026-09-02,
    // the four-tier review's R-11 applied to this writer when the settings
    // editor's device-key arms began carding its failure): the diagnostic
    // carries the full path and the system's words where a call gave any,
    // the display names the file the basename rule's way — `config`, the one
    // file this program writes outside a project — and the same tag opens
    // both. Nothing is printed here; the caller owns its surfaces.
    static constexpr std::string_view kTag =
        "Device config write failed: could not write ";
    const std::filesystem::path path = device_config_path();
    if (path.empty()) {
        // Unreachable after a startup that read the config through the same
        // resolver — the environment does not change under the process — and
        // kept as the belt it always was, with no path to name.
        return plain_failure(
            "Device config write failed: no config home (neither "
            "XDG_CONFIG_HOME nor HOME is set)");
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return path_failure(kTag, path, path.filename().string(),
                            ": " + ec.message());
    }
    // The same atomic writer the sidecars use (tmp + fsync + rename), so a
    // crash mid-write cannot leave a torn file that refuses the next startup.
    // It reports by its bool alone, so this arm has no system words to
    // append — the save's own three arms are in the same position.
    if (!atomic_write_string_to_path(path.string(),
                                     format_device_config_text(cfg))) {
        return path_failure(kTag, path, path.filename().string(), "");
    }
    return std::nullopt;
}

std::expected<DeviceConfig, std::string> load_device_config(
        const DeviceConfig& first_run_template) {
    const std::filesystem::path path = device_config_path();
    if (path.empty()) {
        return std::unexpected(
            std::string("no config home: neither XDG_CONFIG_HOME nor HOME is "
                        "set"));
    }

    // A FAILED STAT READS AS ABSENCE HERE, AND THAT IS ACCEPTED ADVERSARIAL
    // (recorded 2026-09-02, the four-tier review's R-19(e)): `exists` answers
    // false both when the file is not there and when the query itself failed
    // (an unreadable config directory, say), and this arm then goes on to
    // stamp the template — the opposite of `sidecar_present`'s rule one layer
    // up, which treats a failed stat as its own refusal. It is harmless in
    // practice: whatever broke the stat breaks the write on the next line,
    // and startup refuses there with the system's words. Not worth a second
    // error road on a path only a broken config home reaches.
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return std::unexpected("could not create '" +
                                   path.parent_path().string() + "': " +
                                   ec.message());
        }
        // FIRST RUN: stamp the backend's template and then read it back like
        // any other file, so the very first session takes exactly the path
        // every later one does — there is no "apply the template directly"
        // shortcut that could drift from what the reader accepts.
        if (!atomic_write_string_to_path(
                path.string(), format_device_config_text(first_run_template))) {
            return std::unexpected("could not create '" + path.string() + "'");
        }
        std::fprintf(stderr,
            "warptempo_gui: Device config created: %s\n",
            path.string().c_str());
    }

    auto cfg = read_device_config(path);
    if (!cfg) {
        return std::unexpected("invalid device config in '" + path.string() +
                               "': " + cfg.error());
    }
    return *cfg;
}
