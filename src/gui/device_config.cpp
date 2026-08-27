#include "device_config.h"

#include "settings_io.h"       // atomic_write_string_to_path
#include "settings_file.h"     // warptempo_settings::scan_key_value_file
#include "frame_format.h"      // parse_authored_frame
#include "parse_text_util.h"   // warptempo_parse::prefix_line_error

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

// The file's key set, in on-disk order — the writer's order AND the required
// set the shared scanner enforces after the loop. The scanner takes it as a
// SET: it checks that each key ARRIVED, never that it arrived here, so this
// order is the writer's alone and the reader is order-insensitive (the header's
// schema paragraph owns that ruling). One list, so a key cannot be written and
// not demanded (the `.settings` schema keeps the same discipline across two
// lists because its writer is GUI-side and its reader parser-side;
// here both halves are in this file, so one list is the honest shape).
constexpr const char* kDeviceConfigKeys[] = {
    "gui_scale",
    "audio_player",
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
        s += key;
        s += '=';
        if (std::string_view(key) == "gui_scale") {
            s += format_gui_scale_percent(cfg.gui_scale);
        } else {
            // audio_player: free text, emitted verbatim in UTF-8. An empty
            // value writes the bare `audio_player=` line, which is the
            // no-player opt-out and reads back as exactly that.
            s += cfg.audio_player;
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

        if (key == "gui_scale") {
            // One canonical spelling: plain digits through
            // parse_authored_frame (no sign, point, or leading zeros — exactly
            // format_gui_scale_percent's `%d` output), then the RANGE through
            // the one owner in the header.
            int64_t v = 0;
            if (!parse_authored_frame(value, v) || !is_gui_scale_percent(v)) {
                return bad_value(ln, key, value,
                    "must be an integer in [50, 400] in canonical spelling");
            }
            // Range-checked above, so the narrowing to int is exact.
            out.gui_scale = static_cast<int>(v);
            return {};
        }
        if (key == "audio_player") {
            // Any value is accepted (no path or binary grammar — it is
            // user-supplied), INCLUDING empty: an empty value is legal and
            // means "no external player", which is the only spelling of that
            // opt-out. Taken verbatim in UTF-8.
            out.audio_player = value;
            return {};
        }
        return warptempo_parse::prefix_line_error(
            ln, "unknown key '" + key + "'");
    }, kDeviceConfigKeys);
    if (!scan) return std::unexpected(std::move(scan.error()));
    return out;
}

bool write_device_config(const DeviceConfig& cfg) {
    const std::filesystem::path path = device_config_path();
    if (path.empty()) {
        std::fprintf(stderr,
            "warptempo_gui: No config home (neither XDG_CONFIG_HOME nor HOME "
            "is set); device config not written\n");
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: Could not create '%s': %s\n",
            path.parent_path().string().c_str(), ec.message().c_str());
        return false;
    }
    // The same atomic writer the sidecars use (tmp + fsync + rename), so a
    // crash mid-write cannot leave a torn file that refuses the next startup.
    if (!atomic_write_string_to_path(path.string(),
                                     format_device_config_text(cfg))) {
        std::fprintf(stderr,
            "warptempo_gui: Device config write failed: %s\n",
            path.string().c_str());
        return false;
    }
    return true;
}

std::expected<DeviceConfig, std::string> load_device_config(
        const DeviceConfig& first_run_template) {
    const std::filesystem::path path = device_config_path();
    if (path.empty()) {
        return std::unexpected(
            std::string("no config home: neither XDG_CONFIG_HOME nor HOME is "
                        "set"));
    }

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
