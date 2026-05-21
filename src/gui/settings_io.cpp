#include "settings_io.h"

#include "app_state.h"
#include "time_format.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

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

bool parse_int64_full(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = static_cast<int64_t>(v);
    return true;
}

bool parse_int_full(const std::string& s, int& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    if (v < std::numeric_limits<int>::min() ||
        v > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(v);
    return true;
}

bool parse_float_full(const std::string& s, float& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

// Strict-parse helpers shared by validate_engine_setting. Each consumes
// the entire string; trailing garbage is rejected. Non-finite doubles
// are rejected. Integer overflow into out-of-int-range is rejected.

bool parse_double_strict(const std::string& s, double& out) {
    if (s.empty()) return false;
    try {
        std::size_t pos = 0;
        const double v = std::stod(s, &pos);
        if (pos != s.size()) return false;
        if (!std::isfinite(v)) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_int_strict(const std::string& s, int& out) {
    if (s.empty()) return false;
    try {
        std::size_t pos = 0;
        const long v = std::stol(s, &pos, 10);
        if (pos != s.size()) return false;
        if (v < std::numeric_limits<int>::min() ||
            v > std::numeric_limits<int>::max()) return false;
        out = static_cast<int>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_bool_strict(const std::string& s, bool& out) {
    if (s == "true"  || s == "1" || s == "yes" || s == "on")  { out = true;  return true; }
    if (s == "false" || s == "0" || s == "no"  || s == "off") { out = false; return true; }
    return false;
}

// Canonical .settings layout. One descriptor per line in the file, in
// the exact order they appear on disk. Shared by
// format_default_settings_template (template build) and
// write_settings_file (Ctrl+S). Reading is order-insensitive —
// parse_settings_file does not consult this list.
enum class SettingKind {
    EnginePassthrough,
    ActiveAudioViewChar,
    ActiveMarkersViewChar,
    ActiveTabViewChar,
    PlaybackSpeedFloat,
    FollowFlag,
    OptionalTrimBegin_A,
    OptionalTrimEnd_A,
    OptionalTrimBegin_B,
    OptionalTrimEnd_B,
    ReadOnly_A,
    ReadOnly_B,
    ViewportStart_A,
    ZoomLevel_A,
    Playhead_A,
    ViewportStart_B,
    ZoomLevel_B,
    Playhead_B,
};

struct SettingDescriptor {
    const char* key;
    SettingKind kind;
    // Engine-key value source: which field of EngineSettings holds the
    // value. Valid only when kind == EnginePassthrough; ignored otherwise.
    EngineField field;
    // Template default for non-engine kinds. nullptr means "no fixed
    // default" — for the optional trims the template omits the line
    // entirely. For engine kinds, the template default comes from a
    // default-constructed EngineSettings and this field is unused.
    const char* default_value;
};

constexpr SettingDescriptor kSettingsOrder[] = {
    { "title",                       SettingKind::EnginePassthrough,    EngineField::Title,                   nullptr },
    { "scale",                       SettingKind::EnginePassthrough,    EngineField::Scale,                   nullptr },
    { "output_format",               SettingKind::EnginePassthrough,    EngineField::OutputFormat,            nullptr },
    { "N",                           SettingKind::EnginePassthrough,    EngineField::N,                       nullptr },
    { "fftw_threads",                SettingKind::EnginePassthrough,    EngineField::FftwThreads,             nullptr },
    { "limiter_enabled_on_render",   SettingKind::EnginePassthrough,    EngineField::LimiterEnabledOnRender,  nullptr },
    { "phase_reset_offset_hops",     SettingKind::EnginePassthrough,    EngineField::PhaseResetOffsetHops,    nullptr },
    { "limiter_ceiling",             SettingKind::EnginePassthrough,    EngineField::LimiterCeiling,          nullptr },
    { "limiter_attack_ms",           SettingKind::EnginePassthrough,    EngineField::LimiterAttackMs,         nullptr },
    { "limiter_release_ms",          SettingKind::EnginePassthrough,    EngineField::LimiterReleaseMs,        nullptr },
    { "active_audio_view",           SettingKind::ActiveAudioViewChar,  EngineField::Title,                   "S"        },
    { "active_markers_view",         SettingKind::ActiveMarkersViewChar,EngineField::Title,                   "W"        },
    { "active_tab_view",             SettingKind::ActiveTabViewChar,    EngineField::Title,                   "A"        },
    { "playback_speed",              SettingKind::PlaybackSpeedFloat,   EngineField::Title,                   "1.000000" },
    { "follow",                      SettingKind::FollowFlag,           EngineField::Title,                   "true"     },
    { "tab_a_trim_begin",            SettingKind::OptionalTrimBegin_A,  EngineField::Title,                   nullptr },
    { "tab_a_trim_end",              SettingKind::OptionalTrimEnd_A,    EngineField::Title,                   nullptr },
    { "tab_b_trim_begin",            SettingKind::OptionalTrimBegin_B,  EngineField::Title,                   nullptr },
    { "tab_b_trim_end",              SettingKind::OptionalTrimEnd_B,    EngineField::Title,                   nullptr },
    { "tab_a_read_only",             SettingKind::ReadOnly_A,           EngineField::Title,                   "false" },
    { "tab_b_read_only",             SettingKind::ReadOnly_B,           EngineField::Title,                   "false" },
    { "tab_a_viewport_start",        SettingKind::ViewportStart_A,      EngineField::Title,                   "0" },
    { "tab_a_zoom",                  SettingKind::ZoomLevel_A,          EngineField::Title,                   "0" },
    { "tab_a_playhead_cursor",       SettingKind::Playhead_A,           EngineField::Title,                   "0" },
    { "tab_b_viewport_start",        SettingKind::ViewportStart_B,      EngineField::Title,                   "0" },
    { "tab_b_zoom",                  SettingKind::ZoomLevel_B,          EngineField::Title,                   "0" },
    { "tab_b_playhead_cursor",       SettingKind::Playhead_B,           EngineField::Title,                   "0" },
};

// True if `key` is in the EnginePassthrough subset of kSettingsOrder OR
// in the legacy singleton trim set. Used by read_engine_settings_from_file
// to decide whether a line is a non-engine canonical line (skipped) or
// an unknown line (error).
bool is_canonical_non_engine_key(const std::string& key) {
    if (key == "trim_begin" || key == "trim_end") return true;
    for (const auto& desc : kSettingsOrder) {
        if (desc.kind != SettingKind::EnginePassthrough && key == desc.key) {
            return true;
        }
    }
    return false;
}

// Skip predicate for the `.rendersettings` strict engine-block reader.
// View-state lines (bare names, no `render_` prefix) are silently
// skipped; every other non-engine key falls through to the strict
// "unknown engine key" error.
bool is_rendersettings_view_state_key(const std::string& key) {
    return key == "viewport_start" || key == "zoom" || key == "playhead";
}

// Append the value of `field` from `es` to `out`, using the same format
// strings the on-disk template encodes (%.6f for doubles, %d for ints,
// true|false for the limiter flag, raw string for title / output_format).
void append_engine_field_value(std::string& out, const EngineSettings& es,
                                EngineField field) {
    char buf[64];
    switch (field) {
        case EngineField::Title:
            out += es.title;
            break;
        case EngineField::OutputFormat:
            out += es.output_format;
            break;
        case EngineField::Scale:
            std::snprintf(buf, sizeof(buf), "%.6f", es.scale);
            out += buf;
            break;
        case EngineField::N:
            std::snprintf(buf, sizeof(buf), "%d", es.N);
            out += buf;
            break;
        case EngineField::FftwThreads:
            std::snprintf(buf, sizeof(buf), "%d", es.fftw_threads);
            out += buf;
            break;
        case EngineField::LimiterEnabledOnRender:
            out += es.limiter_enabled_on_render ? "true" : "false";
            break;
        case EngineField::PhaseResetOffsetHops:
            std::snprintf(buf, sizeof(buf), "%.6f",
                          es.phase_reset_offset_hops);
            out += buf;
            break;
        case EngineField::LimiterCeiling:
            std::snprintf(buf, sizeof(buf), "%.6f", es.limiter_ceiling);
            out += buf;
            break;
        case EngineField::LimiterAttackMs:
            std::snprintf(buf, sizeof(buf), "%.6f", es.limiter_attack_ms);
            out += buf;
            break;
        case EngineField::LimiterReleaseMs:
            std::snprintf(buf, sizeof(buf), "%.6f", es.limiter_release_ms);
            out += buf;
            break;
    }
}

// Atomic write: tmp + fsync + rename, preserving the existing file's
// permission bits when present. Same shape as the inline I/O inside
// write_settings_file. Shared by write_rendersettings and
// update_rendersettings_view_state.
bool atomic_write_string_to_path(const std::string& path,
                                 const std::string& data) {
    mode_t mode = 0644;
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) mode = st.st_mode & 07777;

    const std::string tmp_path = path + ".tmp";
    int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return false;

    size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written,
                                  data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            ::unlink(tmp_path.c_str());
            return false;
        }
        written += static_cast<size_t>(n);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(tmp_path.c_str());
        return false;
    }
    if (::close(fd) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }
    ::chmod(tmp_path.c_str(), mode);
    if (::rename(tmp_path.c_str(), path.c_str()) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }
    return true;
}

// Append the engine block (the ten canonical engine keys, in
// kSettingsOrder order, byte-identical to the engine block of
// write_settings_file) to `out`. Shared by write_rendersettings.
void append_engine_block(std::string& out, const EngineSettings& engine) {
    for (const auto& desc : kSettingsOrder) {
        if (desc.kind != SettingKind::EnginePassthrough) continue;
        out += desc.key;
        out += '=';
        append_engine_field_value(out, engine, desc.field);
        out += '\n';
    }
}

// Append the three view-state keys (bare names, canonical order) to
// `out`. Shared by write_rendersettings and
// update_rendersettings_view_state.
void append_view_state_block(std::string& out,
                              int64_t viewport_start,
                              int     zoom_level,
                              int64_t playhead) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lld",
                  static_cast<long long>(viewport_start));
    out += "viewport_start=";
    out += buf;
    out += '\n';
    std::snprintf(buf, sizeof(buf), "%d", zoom_level);
    out += "zoom=";
    out += buf;
    out += '\n';
    std::snprintf(buf, sizeof(buf), "%lld",
                  static_cast<long long>(playhead));
    out += "playhead=";
    out += buf;
    out += '\n';
}

// Shared strict engine-block scan. `skip_pred` returns true for
// non-engine keys that should be silent-skipped; every other
// non-engine key is rejected as "unknown engine key". Used by both
// read_engine_settings_from_file (skips canonical non-engine /
// legacy-trim keys) and read_rendersettings_engine_block (skips the
// three bare view-state keys).
std::optional<EngineSettings> read_engine_block_strict(
        const std::string& path,
        bool (*skip_pred)(const std::string&)) {
    auto report = [](const std::string& reason) {
        std::fprintf(stderr,
            "warptempo_gui: engine settings rejected: %s\n", reason.c_str());
    };

    EngineSettings es{};
    bool any_error = false;
    std::set<std::string> seen;

    std::ifstream f(path);
    if (!f) {
        report("could not open '" + path + "'");
        any_error = true;
    } else {
        std::string line;
        while (std::getline(f, line)) {
            const std::string trimmed = trim_ws(line);
            if (trimmed.empty()) continue;
            if (trimmed[0] == '#') continue;
            const size_t eq = trimmed.find('=');
            if (eq == std::string::npos) continue;
            const std::string key   = trim_ws(trimmed.substr(0, eq));
            const std::string value = trim_ws(trimmed.substr(eq + 1));
            if (key.empty()) continue;

            if (skip_pred(key)) continue;

            if (!is_canonical_engine_key(key)) {
                report("unknown engine key '" + key + "'");
                any_error = true;
                continue;
            }
            if (!seen.insert(key).second) {
                report("duplicate key '" + key + "'");
                any_error = true;
                continue;
            }
            std::string reason;
            if (!validate_engine_setting(key, value, es, reason)) {
                report("key '" + key + "' has invalid value '" + value +
                       "': " + reason);
                any_error = true;
                continue;
            }
        }
    }

    auto require = [&](const char* key) {
        if (seen.count(key) == 0) {
            report(std::string("missing required key '") + key + "'");
            any_error = true;
        }
    };
    require("title");
    require("output_format");
    require("scale");
    require("N");
    require("fftw_threads");
    require("limiter_enabled_on_render");
    require("phase_reset_offset_hops");
    require("limiter_ceiling");
    require("limiter_attack_ms");
    require("limiter_release_ms");

    if (any_error) return std::nullopt;
    return es;
}

} // namespace

// MM:SS.mmm shape validator: exactly 9 chars, ':' at index 2, '.' at
// index 5, digits elsewhere. Matches the canonical marker timestamp
// shape parse_timestamp expects. Returns true if parse_timestamp can
// be safely called on `s`. Exposed so settings_editor.cpp can route
// trim_begin / trim_end values through the same predicate.
bool is_settings_timestamp(const std::string& s) {
    if (s.size() != 9) return false;
    if (s[2] != ':' || s[5] != '.') return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 2 || i == 5) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

bool create_if_missing(const std::filesystem::path& p,
                       const std::string& contents) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return true;
    std::ofstream f(p);
    if (!f) {
        std::fprintf(stderr,
                     "warptempo_gui: could not create '%s'\n",
                     p.string().c_str());
        return false;
    }
    f << contents;
    return static_cast<bool>(f);
}

bool parse_settings_file(const std::string& path, ParsedSettings& out) {
    out = ParsedSettings{};
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return true;  // nothing to load
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        const std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = trim_ws(trimmed.substr(0, eq));
        const std::string value = trim_ws(trimmed.substr(eq + 1));
        if (key.empty()) continue;

        if (key == "tab_a_viewport_start") {
            int64_t v;
            if (parse_int64_full(value, v)) { out.has_tab_a_vp = true; out.tab_a_vp = v; }
        } else if (key == "tab_a_zoom") {
            int v;
            if (parse_int_full(value, v)) { out.has_tab_a_zoom = true; out.tab_a_zoom = v; }
        } else if (key == "tab_a_playhead_cursor") {
            int64_t v;
            if (parse_int64_full(value, v)) { out.has_tab_a_ph = true; out.tab_a_ph = v; }
        } else if (key == "tab_b_viewport_start") {
            int64_t v;
            if (parse_int64_full(value, v)) { out.has_tab_b_vp = true; out.tab_b_vp = v; }
        } else if (key == "tab_b_zoom") {
            int v;
            if (parse_int_full(value, v)) { out.has_tab_b_zoom = true; out.tab_b_zoom = v; }
        } else if (key == "tab_b_playhead_cursor") {
            int64_t v;
            if (parse_int64_full(value, v)) { out.has_tab_b_ph = true; out.tab_b_ph = v; }
        } else if (key == "follow") {
            std::string lower = value;
            for (char& c : lower) c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
            if (lower == "true")       { out.has_follow = true; out.follow = true;  }
            else if (lower == "false") { out.has_follow = true; out.follow = false; }
            // Any other value: silent-skip; default (true) applies at the call site.
        } else if (key == "active_audio_view") {
            // Case-sensitive "S" / "T". Anything else silent-skips like
            // the active_markers_view parser.
            if (value == "S") { out.has_active_audio_view = true; out.active_audio_view = 'S'; }
            else if (value == "T") { out.has_active_audio_view = true; out.active_audio_view = 'T'; }
        } else if (key == "active_markers_view") {
            // Case-sensitive "W" / "P" — these literals cross the engine
            // boundary. Anything else silent-skips like the `follow` parser.
            if (value == "W") { out.has_active_markers_view = true; out.active_markers_view = 'W'; }
            else if (value == "P") { out.has_active_markers_view = true; out.active_markers_view = 'P'; }
        } else if (key == "active_tab_view") {
            // Case-sensitive "A" / "B". Anything else silent-skips.
            if (value == "A") { out.has_active_tab_view = true; out.active_tab_view = 'A'; }
            else if (value == "B") { out.has_active_tab_view = true; out.active_tab_view = 'B'; }
        } else if (key == "playback_speed") {
            float v;
            if (parse_float_full(value, v) && v > 0.0f) {
                out.has_playback_speed = true;
                out.playback_speed = v;
            }
        } else if (key == "trim_begin") {
            if (is_settings_timestamp(value)) {
                out.has_trim_begin = true;
                out.trim_begin     = parse_timestamp(value);
            }
        } else if (key == "trim_end") {
            if (is_settings_timestamp(value)) {
                out.has_trim_end = true;
                out.trim_end     = parse_timestamp(value);
            }
        } else if (key == "tab_a_trim_begin") {
            if (is_settings_timestamp(value)) {
                out.has_tab_a_trim_begin = true;
                out.tab_a_trim_begin     = parse_timestamp(value);
            }
        } else if (key == "tab_a_trim_end") {
            if (is_settings_timestamp(value)) {
                out.has_tab_a_trim_end = true;
                out.tab_a_trim_end     = parse_timestamp(value);
            }
        } else if (key == "tab_b_trim_begin") {
            if (is_settings_timestamp(value)) {
                out.has_tab_b_trim_begin = true;
                out.tab_b_trim_begin     = parse_timestamp(value);
            }
        } else if (key == "tab_b_trim_end") {
            if (is_settings_timestamp(value)) {
                out.has_tab_b_trim_end = true;
                out.tab_b_trim_end     = parse_timestamp(value);
            }
        } else if (key == "tab_a_read_only") {
            bool v;
            if (parse_bool_strict(value, v)) {
                out.has_tab_a_read_only = true;
                out.tab_a_read_only     = v;
            }
        } else if (key == "tab_b_read_only") {
            bool v;
            if (parse_bool_strict(value, v)) {
                out.has_tab_b_read_only = true;
                out.tab_b_read_only     = v;
            }
        }
        // Engine keys: deserialized by read_engine_settings_from_file.
        // Unknown keys: silent-skip here; read_engine_settings_from_file
        // surfaces them as errors.
    }
    return true;
}

bool is_canonical_engine_key(const std::string& key) {
    for (const auto& desc : kSettingsOrder) {
        if (desc.kind == SettingKind::EnginePassthrough && key == desc.key) {
            return true;
        }
    }
    return false;
}

bool validate_engine_setting(const std::string& key,
                              const std::string& value,
                              EngineSettings& out,
                              std::string& reason) {
    if (key == "title") {
        // Preserve the prior settings_get behavior: strip a matching
        // leading/trailing double-quote pair, then validate non-empty
        // after whitespace trim and no embedded newline.
        std::string v = value;
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
            v = v.substr(1, v.size() - 2);
        }
        std::size_t a = 0;
        while (a < v.size() &&
               std::isspace(static_cast<unsigned char>(v[a]))) ++a;
        std::size_t b = v.size();
        while (b > a &&
               std::isspace(static_cast<unsigned char>(v[b - 1]))) --b;
        v = v.substr(a, b - a);
        if (v.empty()) {
            reason = "must be non-empty after whitespace trim";
            return false;
        }
        if (v.find('\n') != std::string::npos) {
            reason = "must not contain an embedded newline";
            return false;
        }
        out.title = std::move(v);
        return true;
    }
    if (key == "output_format") {
        if (value != "wav" && value != "timemap" && value != "tempomap") {
            reason = "must be one of {wav, timemap, tempomap}";
            return false;
        }
        out.output_format = value;
        return true;
    }
    if (key == "scale") {
        double v;
        if (!parse_double_strict(value, v) || !(v > 0.0)) {
            reason = "must be a finite double strictly greater than 0";
            return false;
        }
        out.scale = v;
        return true;
    }
    if (key == "N") {
        int v;
        if (!parse_int_strict(value, v) ||
            (v != 1024 && v != 2048 && v != 4096 && v != 8192)) {
            reason = "must be one of {1024, 2048, 4096, 8192}";
            return false;
        }
        out.N = v;
        return true;
    }
    if (key == "fftw_threads") {
        int v;
        if (!parse_int_strict(value, v) || v < 0) {
            reason = "must be an integer >= 0";
            return false;
        }
        out.fftw_threads = v;
        return true;
    }
    if (key == "limiter_enabled_on_render") {
        bool v;
        if (!parse_bool_strict(value, v)) {
            reason = "must be one of {true, false, 1, 0, yes, no, on, off}";
            return false;
        }
        out.limiter_enabled_on_render = v;
        return true;
    }
    if (key == "phase_reset_offset_hops") {
        double v;
        if (!parse_double_strict(value, v) || !(v >= 0.0)) {
            reason = "must be a finite double >= 0";
            return false;
        }
        out.phase_reset_offset_hops = v;
        return true;
    }
    if (key == "limiter_ceiling") {
        double v;
        if (!parse_double_strict(value, v) || !(v <= 0.0)) {
            reason = "must be a finite double <= 0";
            return false;
        }
        out.limiter_ceiling = v;
        return true;
    }
    if (key == "limiter_attack_ms") {
        double v;
        if (!parse_double_strict(value, v) || !(v > 0.0)) {
            reason = "must be a finite double strictly greater than 0";
            return false;
        }
        out.limiter_attack_ms = v;
        return true;
    }
    if (key == "limiter_release_ms") {
        double v;
        if (!parse_double_strict(value, v) || !(v > 0.0)) {
            reason = "must be a finite double strictly greater than 0";
            return false;
        }
        out.limiter_release_ms = v;
        return true;
    }
    reason = "unknown engine key";
    return false;
}

std::optional<EngineSettings> read_engine_settings_from_file(
    const std::string& path) {
    return read_engine_block_strict(path, &is_canonical_non_engine_key);
}

RenderViewState read_rendersettings_view_state(
        const std::filesystem::path& path) {
    RenderViewState out;
    out.zoom_level = kFitFileLevel;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        const std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = trim_ws(trimmed.substr(0, eq));
        const std::string value = trim_ws(trimmed.substr(eq + 1));
        if (key == "viewport_start") {
            int64_t v;
            if (parse_int64_full(value, v)) out.viewport_start = v;
        } else if (key == "zoom") {
            int v;
            if (parse_int_full(value, v)) out.zoom_level = v;
        } else if (key == "playhead") {
            int64_t v;
            if (parse_int64_full(value, v)) out.playhead = v;
        }
        // Engine-block lines and unknown keys: silent-skip.
    }
    return out;
}

std::optional<EngineSettings> read_rendersettings_engine_block(
        const std::filesystem::path& path) {
    return read_engine_block_strict(path.string(),
                                    &is_rendersettings_view_state_key);
}

bool write_rendersettings(const std::filesystem::path& path,
                           const EngineSettings& engine,
                           int64_t viewport_start,
                           int     zoom_level,
                           int64_t playhead) {
    std::string data;
    append_engine_block(data, engine);
    append_view_state_block(data, viewport_start, zoom_level, playhead);
    return atomic_write_string_to_path(path.string(), data);
}

bool update_rendersettings_view_state(const std::filesystem::path& path,
                                       int64_t viewport_start,
                                       int     zoom_level,
                                       int64_t playhead) {
    // Read-modify-write: keep every line whose key isn't one of the
    // three view-state keys, then append the fresh view-state block
    // at the tail. Preserves the engine block and any unknown lines
    // in their existing on-disk order.
    std::string data;
    std::ifstream f(path);
    if (f) {
        std::string line;
        while (std::getline(f, line)) {
            const std::string trimmed = trim_ws(line);
            std::string key;
            const size_t eq = trimmed.find('=');
            if (eq != std::string::npos) {
                key = trim_ws(trimmed.substr(0, eq));
            }
            if (key == "viewport_start" || key == "zoom" ||
                key == "playhead") {
                continue;
            }
            data += line;
            data += '\n';
        }
    } else {
        std::fprintf(stderr,
            "warptempo_gui: render-view: rendersettings missing at '%s'; "
            "writing view-state-only file\n", path.string().c_str());
    }
    append_view_state_block(data, viewport_start, zoom_level, playhead);
    return atomic_write_string_to_path(path.string(), data);
}

std::string format_default_settings_template(const std::string& stem) {
    EngineSettings defaults{};
    defaults.title = stem + "-rendered";
    std::string s;
    for (const auto& desc : kSettingsOrder) {
        if (desc.kind == SettingKind::EnginePassthrough) {
            s += desc.key;
            s += '=';
            append_engine_field_value(s, defaults, desc.field);
            s += '\n';
        } else if (desc.default_value != nullptr) {
            s += desc.key;
            s += '=';
            s += desc.default_value;
            s += '\n';
        }
        // Optional trims (nullptr default, non-engine kind): skipped at
        // template build; the writer emits them at Ctrl+S iff the flag
        // is set.
    }
    return s;
}

bool write_settings_file(
    const std::string& path,
    const ViewState& tab_a,
    const ViewState& tab_b,
    bool follow,
    char active_audio_view,
    char active_markers_view,
    char active_tab_view,
    float playback_speed,
    const EngineSettings& engine) {
    std::string data;
    char buf[64];
    for (const auto& desc : kSettingsOrder) {
        switch (desc.kind) {
            case SettingKind::EnginePassthrough:
                data += desc.key;
                data += '=';
                append_engine_field_value(data, engine, desc.field);
                data += '\n';
                break;
            case SettingKind::ActiveAudioViewChar:
                data += desc.key;
                data += '=';
                data += active_audio_view;
                data += '\n';
                break;
            case SettingKind::ActiveMarkersViewChar:
                data += desc.key;
                data += '=';
                data += active_markers_view;
                data += '\n';
                break;
            case SettingKind::ActiveTabViewChar:
                data += desc.key;
                data += '=';
                data += active_tab_view;
                data += '\n';
                break;
            case SettingKind::PlaybackSpeedFloat:
                std::snprintf(buf, sizeof(buf), "%.6f", playback_speed);
                data += desc.key;
                data += '=';
                data += buf;
                data += '\n';
                break;
            case SettingKind::FollowFlag:
                data += desc.key;
                data += '=';
                data += follow ? "true" : "false";
                data += '\n';
                break;
            case SettingKind::OptionalTrimBegin_A:
                if (tab_a.has_trim_begin) {
                    data += desc.key;
                    data += '=';
                    data += format_timestamp(tab_a.trim_begin_seconds);
                    data += '\n';
                }
                break;
            case SettingKind::OptionalTrimEnd_A:
                if (tab_a.has_trim_end) {
                    data += desc.key;
                    data += '=';
                    data += format_timestamp(tab_a.trim_end_seconds);
                    data += '\n';
                }
                break;
            case SettingKind::OptionalTrimBegin_B:
                if (tab_b.has_trim_begin) {
                    data += desc.key;
                    data += '=';
                    data += format_timestamp(tab_b.trim_begin_seconds);
                    data += '\n';
                }
                break;
            case SettingKind::OptionalTrimEnd_B:
                if (tab_b.has_trim_end) {
                    data += desc.key;
                    data += '=';
                    data += format_timestamp(tab_b.trim_end_seconds);
                    data += '\n';
                }
                break;
            case SettingKind::ReadOnly_A:
                data += desc.key;
                data += '=';
                data += tab_a.read_only ? "true" : "false";
                data += '\n';
                break;
            case SettingKind::ReadOnly_B:
                data += desc.key;
                data += '=';
                data += tab_b.read_only ? "true" : "false";
                data += '\n';
                break;
            case SettingKind::ViewportStart_A:
                std::snprintf(buf, sizeof(buf), "%lld",
                              static_cast<long long>(tab_a.viewport_start_sample));
                data += desc.key; data += '='; data += buf; data += '\n';
                break;
            case SettingKind::ZoomLevel_A:
                std::snprintf(buf, sizeof(buf), "%d", tab_a.zoom_level);
                data += desc.key; data += '='; data += buf; data += '\n';
                break;
            case SettingKind::Playhead_A:
                std::snprintf(buf, sizeof(buf), "%lld",
                              static_cast<long long>(tab_a.playhead_cursor_sample));
                data += desc.key; data += '='; data += buf; data += '\n';
                break;
            case SettingKind::ViewportStart_B:
                std::snprintf(buf, sizeof(buf), "%lld",
                              static_cast<long long>(tab_b.viewport_start_sample));
                data += desc.key; data += '='; data += buf; data += '\n';
                break;
            case SettingKind::ZoomLevel_B:
                std::snprintf(buf, sizeof(buf), "%d", tab_b.zoom_level);
                data += desc.key; data += '='; data += buf; data += '\n';
                break;
            case SettingKind::Playhead_B:
                std::snprintf(buf, sizeof(buf), "%lld",
                              static_cast<long long>(tab_b.playhead_cursor_sample));
                data += desc.key; data += '='; data += buf; data += '\n';
                break;
        }
    }

    mode_t mode = 0644;
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) mode = st.st_mode & 07777;

    const std::string tmp_path = path + ".tmp";
    int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return false;

    size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written,
                                  data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            ::unlink(tmp_path.c_str());
            return false;
        }
        written += static_cast<size_t>(n);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(tmp_path.c_str());
        return false;
    }
    if (::close(fd) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }
    ::chmod(tmp_path.c_str(), mode);
    if (::rename(tmp_path.c_str(), path.c_str()) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }
    return true;
}
