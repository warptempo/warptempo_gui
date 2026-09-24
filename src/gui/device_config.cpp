#include "device_config.h"

#include "settings_io.h"       // atomic_write_string_to_path
#include "settings_file.h"     // warptempo_settings::scan_key_value_file
#include "frame_format.h"      // parse_authored_frame
#include "parse_text_util.h"   // warptempo_parse::prefix_line_error
#include "value_format.h"      // format_value_double / parse_value_double

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace {

// The file's key set, in on-disk order — the writer's order AND the required
// set the shared scanner enforces after the loop (SEVENTEEN keys since the
// six waveform_expander_* tunables were appended 2026-09-24 in the stage's
// own order, after the gain keys; eleven before them, since the
// waveform_gain_* tunables arrived 2026-09-23 in the rule's own order — seven
// arrived, and the expander on L's two left the same day, then the percentile, with
// the upward ratio appended last; fourteen for a day when the upward
// compression's threshold, knee and range were appended 2026-09-24, and
// eleven again the same day when the four upward keys left and
// `waveform_gain_target_db` took the second gain place;
// kWaveformGainKeys and kWaveformExpanderKeys, device_config.h, own their
// names and walls, and the pairing below is checked at compile time; six since
// `max_waveform_height` arrived 2026-09-13 with the waveform cap leaving
// render.h; five from `sync_path`'s arrival 2026-08-30 with the mirror's
// configured destination; four from
// `audio_player`'s retirement 2026-08-28; five from the project model
// 2026-08-27; two before it). THE ORDER IS THE ARCHITECT'S OWN, given with the
// fifth key (2026-08-30): gui_scale, projects_repo, projects_path,
// last_project, sync_path — the sixth placed right after gui_scale
// (architect 2026-09-13). The scanner takes it as a
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
    "waveform_gain_window_s",
    "waveform_gain_target_db",
    "waveform_gain_gate_db",
    "waveform_gain_min_fraction",
    "waveform_gain_max",
    "waveform_expander_threshold_db",
    "waveform_expander_ratio",
    "waveform_expander_range_db",
    "waveform_expander_knee_db",
    "waveform_expander_hold_ms",
    "waveform_expander_release_ms",
};

constexpr size_t kGainKeyCount     = std::size(kWaveformGainKeys);
constexpr size_t kExpanderKeyCount = std::size(kWaveformExpanderKeys);
constexpr size_t kGainKeysFirst =
    std::size(kDeviceConfigKeys) - kGainKeyCount - kExpanderKeyCount;
constexpr size_t kExpanderKeysFirst = kGainKeysFirst + kGainKeyCount;

// The eleven names are spelled twice — here as the emitted list, in the
// header as the two grammar tables — and this is what keeps them one list:
// the tail of kDeviceConfigKeys IS kWaveformGainKeys then
// kWaveformExpanderKeys, name for name and in order.
consteval bool tunable_keys_are_the_tail() {
    for (size_t i = 0; i < kGainKeyCount; ++i) {
        if (std::string_view(kDeviceConfigKeys[kGainKeysFirst + i]) !=
            std::string_view(kWaveformGainKeys[i].key))
            return false;
    }
    for (size_t i = 0; i < kExpanderKeyCount; ++i) {
        if (std::string_view(kDeviceConfigKeys[kExpanderKeysFirst + i]) !=
            std::string_view(kWaveformExpanderKeys[i].key))
            return false;
    }
    return true;
}
static_assert(tunable_keys_are_the_tail(),
              "kDeviceConfigKeys' tail must be kWaveformGainKeys then "
              "kWaveformExpanderKeys, in order");

// The row named `key` in `table`, or nullptr.
template <class Params, size_t N>
const WaveformTunableKey<Params>* find_tunable_key(
        const WaveformTunableKey<Params> (&table)[N], std::string_view key) {
    for (const WaveformTunableKey<Params>& k : table)
        if (key == k.key) return &k;
    return nullptr;
}

} // namespace

std::string format_waveform_gain_value(double v) {
    // format_value_double prints a negative value's own '-' (std::to_chars),
    // so the one serializer covers the dB key's negative values too.
    return format_value_double(v, 2);
}

bool parse_waveform_gain_value(const std::string& s, double& out) {
    // parse_value_double refuses a leading '-' (no authored value is
    // negative), so the sign is taken here and the magnitude handed on.
    const bool negative = !s.empty() && s.front() == '-';
    double magnitude = 0.0;
    if (!parse_value_double(std::string_view(s).substr(negative ? 1 : 0), magnitude))
        return false;
    if (negative && magnitude == 0.0) return false;   // zero has one spelling
    const double v = negative ? -magnitude : magnitude;
    // ONE CANONICAL SPELLING: the text must be exactly what the writer emits
    // for the value it names (`1.5`, `1.500` and `+1.50` all refuse).
    if (format_waveform_gain_value(v) != s) return false;
    out = v;
    return true;
}

std::string waveform_gain_grammar_reason(double lo, double hi) {
    return "must be a number in [" + format_waveform_gain_value(lo) + ", " +
           format_waveform_gain_value(hi) + "] in canonical spelling";
}

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
        } else if (const WaveformGainKey* g = find_tunable_key(kWaveformGainKeys, k)) {
            // The five waveform_gain_* tunables, through their one
            // serializer.
            s += format_waveform_gain_value(cfg.waveform_gain.*(g->member));
        } else {
            // The six waveform_expander_* tunables, through the same
            // serializer; the static_assert above makes this arm total.
            s += format_waveform_gain_value(
                cfg.waveform_expander.*(find_tunable_key(kWaveformExpanderKeys, k)->member));
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
        if (const WaveformGainKey* g = find_tunable_key(kWaveformGainKeys, key)) {
            // One canonical spelling through the one parser, then the
            // bracket — both owned in the header (kWaveformGainKeys).
            double v = 0.0;
            if (!parse_waveform_gain_value(value, v) ||
                !is_waveform_gain_value(*g, v)) {
                return bad_value(ln, key, value, waveform_gain_grammar_reason(*g));
            }
            out.waveform_gain.*(g->member) = v;
            return {};
        }
        if (const WaveformExpanderKey* e = find_tunable_key(kWaveformExpanderKeys, key)) {
            // The same grammar over the expander's table
            // (kWaveformExpanderKeys).
            double v = 0.0;
            if (!parse_waveform_gain_value(value, v) ||
                !is_waveform_gain_value(*e, v)) {
                return bad_value(ln, key, value, waveform_gain_grammar_reason(*e));
            }
            out.waveform_expander.*(e->member) = v;
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
