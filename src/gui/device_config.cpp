#include "device_config.h"

#include "settings_io.h"       // atomic_write_string_to_path
#include "settings_file.h"     // warptempo_settings::scan_key_value_file
#include "frame_format.h"      // parse_authored_frame
#include "parse_text_util.h"   // warptempo_parse::prefix_line_error
#include "theme_file.h"        // is_theme_key, kThemeGrammarReason; through
                               // render.h and gui_font.h, chrome_spec.h's
                               // is_chrome_key, kChromeGrammarReason
#include "palette_file.h"      // is_palette_name, kPaletteGrammarReason

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace {

// The file's key set, in on-disk order — the writer's order, the required
// set below its subset (SEVEN keys since 2026-10-07 evening; the count's
// succession, up to seventeen with the tuning phases of 2026-09-23..27 and
// nineteen with the colour keys of 2026-10-03..04, is the header's record
// and git's). THE ORDER IS THE ARCHITECT'S OWN, given
// with the fifth key (2026-08-30): gui_scale, projects_repo, projects_path,
// last_project. The scanner takes it as a
// SET: it checks that each key ARRIVED, never that it arrived here, so this
// order is the writer's alone and the reader is order-insensitive (the header's
// schema paragraph owns that ruling). The writer's list and the required
// list stand side by side below, the second the first less its three keys
// that may be absent (2026-10-07; until then one list served both, so no
// key could be written and not demanded — the absent-able keys are the
// deliberate exception, each saying what its absence means).
//
// `theme` IS APPENDED (2026-10-03); `chrome` STANDS BEFORE IT (2026-10-07),
// the key whose vocabulary names the theme a file without a `theme` line
// wears; `palette` FOLLOWS IT (2026-10-07), the program's colors after the
// chrome's.
constexpr const char* kDeviceConfigKeys[] = {
    "gui_scale",
    "projects_repo",
    "projects_path",
    "last_project",
    "chrome",
    "theme",
    "palette",
};
// THE REQUIRED SET — every key above but the three that may be ABSENT
// (architect 2026-10-07): `chrome`, absent reading as clearlooks (the
// default chrome since 2026-10-07 ~16:00, kDefaultChromeKey; a config
// written before the key existed loads, in the default chrome), `theme`,
// absent meaning the chrome's own theme, and `palette`, absent meaning the
// chrome's default palette. The scanner
// checks presence against this list and duplicates against every key.
constexpr const char* kDeviceConfigRequiredKeys[] = {
    "gui_scale",
    "projects_repo",
    "projects_path",
    "last_project",
};

} // namespace

std::string format_gui_scale_percent(int percent) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", percent);
    return std::string(buf);
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
        // AN UNSET THEME WRITES NO LINE (2026-10-07): its one spelling is
        // the line's absence, so the file keeps following the chrome.
        if (std::string_view(key) == "theme" && cfg.theme.empty()) continue;
        // AN UNSET PALETTE WRITES NO LINE (2026-10-07), the theme's shape.
        if (std::string_view(key) == "palette" && cfg.palette.empty())
            continue;
        s += key;
        s += '=';
        const std::string_view k(key);
        // The arms are in the emitted order above, so this reads as the file
        // reads.
        if (k == "gui_scale") {
            s += format_gui_scale_percent(cfg.gui_scale);
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
        } else if (k == "chrome") {
            // The vocabulary's key verbatim, always written (the default's
            // too, so a file once rewritten names its chrome).
            s += cfg.chrome;
        } else if (k == "theme") {
            s += cfg.theme;
        } else if (k == "palette") {
            // The name verbatim (printable ASCII, palette_file.h's grammar).
            s += cfg.palette;
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
        // THE CHROME (architect 2026-10-07): a vocabulary's key under its one
        // grammar owner (is_chrome_key, chrome_spec.h); absent, the struct's
        // clearlooks stands (kDefaultChromeKey).
        if (key == "chrome") {
            if (!is_chrome_key(value)) {
                return bad_value(ln, key, value, kChromeGrammarReason);
            }
            out.chrome = value;
            return {};
        }
        // THE THEME (2026-10-03): the built-in's key or a theme file's read
        // at launch, under its one grammar owner (is_theme_key, theme_file.h)
        // — which is why gui_main reads the themes folder BEFORE this file.
        // An EMPTY value is refused like any other non-key: the unset theme
        // is the line's absence (2026-10-07), which the writer emits.
        if (key == "theme") {
            if (!is_theme_key(value)) {
                return bad_value(ln, key, value, kThemeGrammarReason);
            }
            out.theme = value;
            return {};
        }
        // THE PALETTE (2026-10-07): a default's name or a palette file's read
        // at launch, under its one grammar owner (is_palette_name,
        // palette_file.h) — which is why gui_main reads the palettes folder
        // BEFORE this file. An EMPTY value is refused like any other
        // non-name: the unset palette is the line's absence.
        if (key == "palette") {
            if (!is_palette_name(value)) {
                return bad_value(ln, key, value, kPaletteGrammarReason);
            }
            out.palette = value;
            return {};
        }
        return warptempo_parse::prefix_line_error(
            ln, "unknown key '" + key + "'");
    }, kDeviceConfigRequiredKeys);
    if (!scan) return std::unexpected(std::move(scan.error()));
    return out;
}

std::optional<GuiFailure> write_device_config(const DeviceConfig& cfg) {
    // THE TWO CLAUSES ARE COMPOSED HERE (GuiFailure, failure.h — 2026-09-02,
    // the two-clause rule applied to this writer when the settings
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
    // (recorded 2026-09-02): `exists` answers
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
