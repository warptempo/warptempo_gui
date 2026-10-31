#include "device_config.h"

#include "settings_io.h"       // atomic_write_string_to_path
#include "settings_file.h"     // warptempo_settings::scan_key_value_file
#include "frame_format.h"      // parse_authored_frame
#include "parse_text_util.h"   // warptempo_parse::prefix_line_error
#include "palette_file.h"      // is_builtin_palette_name and its reason

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace {

// The file's key set, in on-disk order — the writer's order, and THE
// REQUIRED SET, every one of them (THIRTEEN keys since 2026-10-30, `role`
// retired with the laptop GUI — one device, no mirror; FOURTEEN 2026-10-29
// night, the keyboard's cap size `osk_font_px` appended at the end; THIRTEEN
// that evening, the keyboard's two tuning keys `osk_key_w` and `osk_key_h`
// appended at the end; ELEVEN that morning, the tuning key `key_height`
// appended at the end; TEN from 2026-10-28, the noise's
// three appended — `noise`, `noise_level` and `noise_crossfade_s`, all
// permanent, the crossfade the tuning key `noise_crossfade_ms` until that
// night; NINE earlier that day, the level and
// the crossfade appended as `rain_level` and `rain_crossfade_ms` before the
// rename and the folder key's arrival; ELEVEN for part of that day, two fade
// tuning keys appended after the crossfade at ~11:30 and settled the same
// day at 200 and 40 ms, noise.h's constexprs; SEVEN 2026-10-26, the LED
// glow settled to a constant, materials.cpp's kLedGlowPx; EIGHT 2026-10-23,
// `strip_pad` settled to a constant, program_spec.h; nine that day, the
// bevel pair gone and the LED glow's key appended; TEN 2026-10-21 evening,
// `true_colors` placed after `waveform`, below; nine that afternoon, the three
// tuning keys appended, `caption` gone and `chrome` renamed
// `waveform`; seven 2026-10-12, `icons` and `font` gone;
// nine 2026-10-10 evening, `role` appended; eight 2026-10-10,
// `chrome` gone; nine 2026-10-09 ~21:20, `font` appended; eight 2026-10-09,
// `icons` appended; seven 2026-10-08, `theme` gone; the
// count's succession, up to seventeen with the tuning phases of
// 2026-09-23..27 and nineteen with the colour keys of 2026-10-03..04, is the
// header's record and git's). THE ORDER IS THE ARCHITECT'S OWN, given
// with the fifth key (2026-08-30): gui_scale, projects_repo, projects_path,
// last_project. The scanner takes it as a
// SET: it checks that each key ARRIVED, never that it arrived here, so this
// order is the writer's alone and the reader is order-insensitive (the header's
// schema paragraph owns that ruling). ONE LIST SERVES BOTH, the writer's
// order and the scanner's presence check (architect 2026-10-14, every key
// required), so no key can be written and not demanded — the rule the
// retired `role` key (2026-10-10) brought, "if one of these lines is not in
// place, it means something's wrong with the config or the setup"
// (architect), and every key follows.
//
// THE KINDS' KEYS RENAMED (architect 2026-10-11 ~03:05, go 2026-10-13; and
// 2026-10-21): `waveform` (`palette` until 2026-10-13, then `chrome` until
// 2026-10-21, renamed for what it sets) FOLLOWS last_project, where the
// `caption` key (formerly `scheme`) stood before it until it left
// 2026-10-21. (`theme` stood
// 2026-10-03..10-08, `icons` and `font` 2026-10-09 to 2026-10-12, the
// header's record.)
//
// TRUE COLORS (architect 2026-10-21, the Settings popup's command row made a
// key): `true_colors` FOLLOWS `waveform`, the two display keys together, and
// the tuning keys keep the end where they leave from.
//
// THE NOISE'S THREE (architect 2026-10-28): `noise`, `noise_level` and
// `noise_crossfade_s` FOLLOW `true_colors`, the permanent keys' end, the
// folder before its loudness and its crossfade.
//
// THE TUNING KEYS stand after the permanent keys, where the tuning phases'
// keys have always stood, each leaving when he settles its value:
// `key_height` since 2026-10-29, and the on-screen keyboard's key box,
// `osk_key_w` and `osk_key_h`, after it since that evening, and the
// keyboard's cap size, `osk_font_px`, after them since that night (none
// stood from 2026-10-28 night,
// when `noise_crossfade_ms`, the crossfade's tuning key that day, was made
// the permanent seconds key above).
// (`rain_fade_in_ms` and `rain_fade_out_ms` stood after it one day,
// 2026-10-28, settled at 200 and 40 ms, noise.h's kNoiseFadeInMs and
// kNoiseFadeOutMs; the LED glow's stood there 2026-10-23 to 2026-10-26,
// settled at 4 device px, materials.cpp's kLedGlowPx; `strip_pad` before it
// 2026-10-22 to 2026-10-23, settled at 13 W, program_spec.h's strip_pad_px;
// `bevel_light` and `bevel_dark` 2026-10-21 to 2026-10-23, `text_shadow`
// beside them until 2026-10-22; the cues' seven after it 2026-10-26, settled
// that day, the header's record.)
constexpr const char* kDeviceConfigKeys[] = {
    "gui_scale",
    "projects_repo",
    "projects_path",
    "last_project",
    "waveform",
    "true_colors",
    "noise",
    "noise_level",
    "noise_crossfade_s",
    "key_height",
    "osk_key_w",
    "osk_key_h",
    "osk_font_px",
};
// THE REQUIRED KEYS: every key of the writer's order (no optional key since
// 2026-10-26).
constexpr std::span<const char* const> kDeviceConfigRequiredKeys(
    kDeviceConfigKeys);
} // namespace

std::string format_gui_scale_percent(int percent) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", percent);
    return std::string(buf);
}

// The two noise integers' canonical spellings (2026-10-28): `%d`, exactly
// what parse_canonical_config_int admits back (device_config.h).
std::string format_noise_level(int db) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", db);
    return std::string(buf);
}

std::string format_noise_crossfade_s(int s) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", s);
    return std::string(buf);
}

// The key's height's canonical spelling (2026-10-29): `%d`, the noise
// integers' road.
std::string format_key_height(int px) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", px);
    return std::string(buf);
}

// The keyboard's key box's canonical spelling (2026-10-29), one for its two
// keys: `%d`, the noise integers' road.
std::string format_osk_key_size(int px) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", px);
    return std::string(buf);
}

// The keyboard's cap size's canonical spelling (2026-10-29 night): `%d`,
// the noise integers' road.
std::string format_osk_font_px(int px) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", px);
    return std::string(buf);
}

std::filesystem::path device_config_path() {
    // XDG_CONFIG_HOME AND NOTHING ELSE (architect 2026-10-30: the tablet is
    // the one platform), the render cache's own shape (RenderCache::init,
    // render_cache.cpp) one variable over. android_main sets it before
    // gui_main runs, so an unset variable is a program fault: the empty
    // answer, which the reader refuses at startup with one line.
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (!xdg || !xdg[0]) return {};
    return std::filesystem::path(xdg) / "warptempo_gui" / "config";
}

std::string format_device_config_text(const DeviceConfig& cfg) {
    std::string s;
    for (const char* key : kDeviceConfigKeys) {
        // EVERY KEY WRITES ITS LINE (architect 2026-10-14: every key is
        // required and always named).
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
        } else if (k == "waveform") {
            // The built-in's key verbatim (palette_file.h's key grammar).
            s += cfg.waveform;
        } else if (k == "true_colors") {
            // The switch's word (format_true_colors, the grammar's owner).
            s += format_true_colors(cfg.true_colors);
        } else if (k == "noise") {
            // The folder's name verbatim (is_noise_folder_name's grammar).
            s += cfg.noise;
        } else if (k == "noise_level") {
            // The level's canonical integer (format_noise_level).
            s += format_noise_level(cfg.noise_level);
        } else if (k == "noise_crossfade_s") {
            // The crossfade's canonical integer (format_noise_crossfade_s).
            s += format_noise_crossfade_s(cfg.noise_crossfade_s);
        } else if (k == "key_height") {
            // The tuning key's canonical integer (format_key_height).
            s += format_key_height(cfg.key_height);
        } else if (k == "osk_key_w") {
            // The keyboard's tuning keys' canonical integer
            // (format_osk_key_size).
            s += format_osk_key_size(cfg.osk_key_w);
        } else if (k == "osk_key_h") {
            s += format_osk_key_size(cfg.osk_key_h);
        } else if (k == "osk_font_px") {
            // The keyboard's cap size's canonical integer
            // (format_osk_font_px).
            s += format_osk_font_px(cfg.osk_font_px);
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
        // THE WAVEFORM PRESET (2026-10-07 as `palette`, `chrome` 2026-10-13,
        // `waveform` 2026-10-21): A BUILT-IN'S KEY ALONE since 2026-10-14
        // (architect, "remove picker and preset file road": the preset files
        // are gone), under its one grammar owner (is_builtin_palette_name,
        // palette_file.h) — any other word, a former file's name included,
        // the ordinary bad-value hard fail with no migration. An EMPTY value
        // is refused like any other non-name, and an ABSENT line is the
        // scanner's missing-required-key refusal (architect 2026-10-14: every
        // key required, the default spelled —
        // `waveform=default`).
        // An old `palette=`, `chrome=`, `scheme=` or `caption=` line is the
        // unknown key below (the `caption` key's arm stood here
        // 2026-10-08..10-21).
        if (key == "waveform") {
            if (!is_builtin_palette_name(value)) {
                return bad_value(ln, key, value, kPaletteGrammarReason);
            }
            out.waveform = value;
            return {};
        }
        // TRUE COLORS (2026-10-21): one of the two words under its one owner
        // (parse_true_colors, device_config.h), required like every key; any
        // other word the launch's hard fail.
        if (key == "true_colors") {
            const std::optional<bool> on = parse_true_colors(value);
            if (!on) {
                return bad_value(ln, key, value, kTrueColorsGrammarReason);
            }
            out.true_colors = *on;
            return {};
        }
        // THE NOISE'S THREE KEYS (2026-10-28): each under its one grammar
        // owner (is_noise_folder_name, parse_noise_level,
        // parse_noise_crossfade_s, device_config.h), required like every key;
        // any other spelling the launch's hard fail. The folder's name is
        // judged by its shape alone: a name whose folder is absent loads, and
        // the key greys (noise.h). (`rain_level` and `rain_crossfade_ms`,
        // renamed the same day, `rain_fade_in_ms` and `rain_fade_out_ms`,
        // settled that day, and `noise_crossfade_ms`, made seconds that
        // night, fall to the unknown-key arm below like every retired key.)
        if (key == "noise") {
            if (!is_noise_folder_name(value)) {
                return bad_value(ln, key, value, kNoiseFolderGrammarReason);
            }
            out.noise = value;
            return {};
        }
        if (key == "noise_level") {
            const std::optional<int> db = parse_noise_level(value);
            if (!db) {
                return bad_value(ln, key, value, kNoiseLevelGrammarReason);
            }
            out.noise_level = *db;
            return {};
        }
        if (key == "noise_crossfade_s") {
            const std::optional<int> s = parse_noise_crossfade_s(value);
            if (!s) {
                return bad_value(ln, key, value,
                                 kNoiseCrossfadeGrammarReason);
            }
            out.noise_crossfade_s = *s;
            return {};
        }
        // THE KEY'S HEIGHT (2026-10-29, a tuning key): under its one grammar
        // owner (parse_key_height, device_config.h), required like every key;
        // any other spelling the launch's hard fail.
        if (key == "key_height") {
            const std::optional<int> px = parse_key_height(value);
            if (!px) {
                return bad_value(ln, key, value, kKeyHeightGrammarReason);
            }
            out.key_height = *px;
            return {};
        }
        // THE KEYBOARD'S KEY BOX (2026-10-29, two tuning keys): each under
        // the one grammar owner (parse_osk_key_size, device_config.h),
        // required like every key; any other spelling the launch's hard fail.
        if (key == "osk_key_w" || key == "osk_key_h") {
            const std::optional<int> px = parse_osk_key_size(value);
            if (!px) {
                return bad_value(ln, key, value, kOskKeySizeGrammarReason);
            }
            (key == "osk_key_w" ? out.osk_key_w : out.osk_key_h) = *px;
            return {};
        }
        // THE KEYBOARD'S CAP SIZE (2026-10-29 night, a tuning key): under its
        // one grammar owner (parse_osk_font_px, device_config.h), required
        // like every key; any other spelling the launch's hard fail.
        if (key == "osk_font_px") {
            const std::optional<int> px = parse_osk_font_px(value);
            if (!px) {
                return bad_value(ln, key, value, kOskFontPxGrammarReason);
            }
            out.osk_font_px = *px;
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
            "Device config write failed: no config home (XDG_CONFIG_HOME "
            "is not set)");
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
            std::string("no config home: XDG_CONFIG_HOME is not set"));
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
