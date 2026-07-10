#include "settings_io.h"

#include "app_state.h"
#include "parser/parse_text_util.h"
#include "playback_speed_presets.h"
#include "render_pipeline.h"
#include "settings_trim.h"
#include "frame_format.h"
#include "value_format.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace {

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

bool parse_double_full(const std::string& s, double& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

// Strict whole-token bool parser shared by parse_settings_file (the
// tab_*_read_only branches). Accepts the canonical truthy/falsy token set.

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
    FontSizePt,
    TrimBegin_A,
    TrimEnd_A,
    TrimBegin_B,
    TrimEnd_B,
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
    { "bpm",                         SettingKind::EnginePassthrough,    EngineField::Bpm,                     nullptr },
    { "notes",                       SettingKind::EnginePassthrough,    EngineField::Notes,                   nullptr },
    { "url",                         SettingKind::EnginePassthrough,    EngineField::Url,                     nullptr },
    { "cover",                       SettingKind::EnginePassthrough,    EngineField::Cover,                   nullptr },
    { "output_format",               SettingKind::EnginePassthrough,    EngineField::OutputFormat,            nullptr },
    { "limiter",                     SettingKind::EnginePassthrough,    EngineField::Limiter,                 nullptr },
    // GUI view-state band begins: non-tab keys, then all tab A keys,
    // then all tab B keys.
    { "active_audio_view",           SettingKind::ActiveAudioViewChar,  EngineField::Title,                   "S"        },
    { "active_markers_view",         SettingKind::ActiveMarkersViewChar,EngineField::Title,                   "W"        },
    { "active_tab_view",             SettingKind::ActiveTabViewChar,    EngineField::Title,                   "A"        },
    { "playback_speed",              SettingKind::PlaybackSpeedFloat,   EngineField::Title,                   "1.0" },
    { "follow",                      SettingKind::FollowFlag,           EngineField::Title,                   "true"     },
    // GUI-kind key, NOT an engine key: the single GUI-wide monospace text
    // size in points (pixels = points * 4/3). Valid range 6..72. Like
    // playback_speed, loading a source applies the file's value, so a load
    // can change the GUI text size mid-session.
    { "font_size",                   SettingKind::FontSizePt,           EngineField::Title,                   "11"       },
    { "tab_a_trim_begin",            SettingKind::TrimBegin_A,          EngineField::Title,                   nullptr },
    { "tab_a_trim_end",              SettingKind::TrimEnd_A,            EngineField::Title,                   nullptr },
    { "tab_a_read_only",             SettingKind::ReadOnly_A,           EngineField::Title,                   "false" },
    { "tab_a_viewport_start",        SettingKind::ViewportStart_A,      EngineField::Title,                   "0" },
    { "tab_a_zoom",                  SettingKind::ZoomLevel_A,          EngineField::Title,                   "0" },
    { "tab_a_playhead_cursor",       SettingKind::Playhead_A,           EngineField::Title,                   "0" },
    { "tab_b_trim_begin",            SettingKind::TrimBegin_B,          EngineField::Title,                   nullptr },
    { "tab_b_trim_end",              SettingKind::TrimEnd_B,            EngineField::Title,                   nullptr },
    { "tab_b_read_only",             SettingKind::ReadOnly_B,           EngineField::Title,                   "false" },
    { "tab_b_viewport_start",        SettingKind::ViewportStart_B,      EngineField::Title,                   "0" },
    { "tab_b_zoom",                  SettingKind::ZoomLevel_B,          EngineField::Title,                   "0" },
    { "tab_b_playhead_cursor",       SettingKind::Playhead_B,           EngineField::Title,                   "0" },
};

// Append the value of `field` from `es` to `out`, using the same format
// the on-disk template encodes (padded shortest round-trip at min 4
// decimals for scale — value_format.h — true|false for the limiter flag,
// raw string for title / output_format and the provenance fields).
void append_engine_field_value(std::string& out, const EngineSettings& es,
                                EngineField field) {
    switch (field) {
        case EngineField::Title:
            out += es.title;
            break;
        case EngineField::OutputFormat:
            out += es.output_format;
            break;
        case EngineField::Scale:
            out += format_value_double(es.scale, 4);
            break;
        case EngineField::Bpm:
            // bpm is a descriptor string; emit verbatim, empty when unset.
            out += es.bpm;
            break;
        case EngineField::Notes:
            out += es.notes;
            break;
        case EngineField::Url:
            out += es.url;
            break;
        case EngineField::Cover:
            out += es.cover;
            break;
        case EngineField::Limiter:
            out += es.limiter ? "true" : "false";
            break;
    }
}

// Append the engine block (the eight canonical engine keys, in
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

void append_authoring_block(std::string& out,
                            const AuthoringSnapshot& authoring) {
    if (!authoring.valid) return;
    char buf[64];
    out += "active_tab=";
    out += authoring.active_tab;
    out += '\n';
    out += "active_audio_view=";
    out += authoring.active_audio_view;
    out += '\n';
    // Trim bounds are authored positions (whole source frames); they
    // serialize as integer text through the canonical authored pair in
    // frame_format.h, same as every other authored position on disk.
    if (authoring.has_trim_begin) {
        out += "trim_begin=";
        out += format_authored_frame(authoring.trim_begin_frame);
        out += '\n';
    }
    if (authoring.has_trim_end) {
        out += "trim_end=";
        out += format_authored_frame(authoring.trim_end_frame);
        out += '\n';
    }
    std::snprintf(buf, sizeof(buf), "%d", authoring.zoom_level);
    out += "authoring_zoom=";
    out += buf;
    out += '\n';
    std::snprintf(buf, sizeof(buf), "%lld",
                  static_cast<long long>(authoring.viewport_start));
    out += "authoring_viewport_start=";
    out += buf;
    out += '\n';
    std::snprintf(buf, sizeof(buf), "%lld",
                  static_cast<long long>(authoring.playhead));
    out += "authoring_playhead=";
    out += buf;
    out += '\n';
}

} // namespace

// Atomic write: tmp + fsync + rename, preserving the existing file's
// permission bits when present (0644 fallback). Shared by
// write_rendersettings, update_rendersettings_view_state,
// write_settings_file, and the warp/phase-reset marker writers.
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

bool create_if_missing(const std::filesystem::path& p,
                       const std::string& contents) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return true;
    // First-open templates are written atomically so a crash cannot leave a
    // torn file that blocks future strict loads.
    if (!atomic_write_string_to_path(p.string(), contents)) {
        std::fprintf(stderr,
                     "warptempo_gui: could not create '%s'\n",
                     p.string().c_str());
        return false;
    }
    return true;
}

bool parse_settings_file(const std::string& path, ParsedSettings& out) {
    out = ParsedSettings{};
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return true;  // nothing to load
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    bool first_line = true;
    while (std::getline(f, line)) {
        if (first_line) {
            warptempo_parse::strip_bom(line);
            first_line = false;
        }
        const std::string trimmed = warptempo_parse::trim_ws(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = warptempo_parse::trim_ws(trimmed.substr(0, eq));
        const std::string value = warptempo_parse::trim_ws(trimmed.substr(eq + 1));
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
            // Case-sensitive "W" / "P". Anything else silent-skips like
            // the `follow` parser.
            if (value == "W") { out.has_active_markers_view = true; out.active_markers_view = 'W'; }
            else if (value == "P") { out.has_active_markers_view = true; out.active_markers_view = 'P'; }
        } else if (key == "active_tab_view") {
            // This branch stores the view-state copy of the key; strict
            // validation is owned by read_settings_trim below, which
            // hardfails a duplicate key or any value but A or B. Unlike its
            // active_audio_view and active_markers_view siblings, a
            // malformed value here is load-fatal rather than skippable:
            // active_tab_view selects which tab's trim the GUI render and
            // the headless CLIs apply, so silently defaulting it could
            // render the wrong window.
            if (value == "A") { out.has_active_tab_view = true; out.active_tab_view = 'A'; }
            else if (value == "B") { out.has_active_tab_view = true; out.active_tab_view = 'B'; }
        } else if (key == "playback_speed") {
            // playback_speed is preset-vocabulary-only on disk: the GUI
            // authors it exclusively through the Shift+0..9 presets
            // (kPlaybackSpeedPresets, the shared source of truth), so any
            // off-preset value is a state the GUI can never produce —
            // adversarial by the two-category rule — and aborts the source
            // load, the same hard-fail shape a malformed engine key produces.
            // The slow-down presets are the precision-finetune tool.
            //
            // Exact float equality against the table is sound: the writer
            // emits each preset with the same one-decimal %.1f the reader parses back,
            // and both the on-disk text and the table literal are the nearest
            // float of the same short decimal, so parse(write(preset)) equals
            // the literal bit-for-bit. No tolerance.
            float v;
            if (!parse_float_full(value, v) || !is_playback_speed_preset(v)) {
                std::fprintf(stderr,
                    "warptempo_gui: playback_speed '%s' in '%s' is not a "
                    "preset speed\n",
                    value.c_str(), path.c_str());
                return false;
            }
            out.has_playback_speed = true;
            out.playback_speed = v;
        } else if (key == "font_size") {
            // GUI font size in points, strict whole-token double, 6..72
            // inclusive (parse_double_full already rejects non-finite).
            // Unlike the sibling `follow` branch, a bad value gets one
            // diagnostic line rather than a silent skip: font_size is
            // range-constrained and hand-editable, and silently snapping a
            // typo to the default would be silently-correcting.
            double v;
            if (parse_double_full(value, v) && v >= 6.0 && v <= 72.0) {
                out.has_font_size = true;
                out.font_size = v;
            } else {
                std::fprintf(stderr,
                    "warptempo_gui: ignoring font_size='%s' in '%s' "
                    "(not a number in [6, 72]); default 11 applies\n",
                    value.c_str(), path.c_str());
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
        // Engine keys and unknown keys are silent-skipped here; the strict
        // engine reader also ignores non-canonical keys.
    }

    // Per-tab trim bounds: parsed by the parser library's reader so the
    // parser and render CLIs share one implementation with the GUI.
    const auto trim_result = read_settings_trim(path);
    if (!trim_result) {
        std::fprintf(stderr,
            "warptempo_gui: trim settings rejected in '%s': %s\n",
            path.c_str(),
            trim_result.error().c_str());
        return false;
    }
    const SettingsTrimTabs& t = *trim_result;
    out.has_tab_a_trim_begin = t.tab_a.has_begin; out.tab_a_trim_begin = t.tab_a.begin_frame;
    out.has_tab_a_trim_end   = t.tab_a.has_end;   out.tab_a_trim_end   = t.tab_a.end_frame;
    out.has_tab_b_trim_begin = t.tab_b.has_begin; out.tab_b_trim_begin = t.tab_b.begin_frame;
    out.has_tab_b_trim_end   = t.tab_b.has_end;   out.tab_b_trim_end   = t.tab_b.end_frame;
    return true;
}

RenderViewState read_rendersettings_view_state(
        const std::filesystem::path& path) {
    RenderViewState out;
    out.zoom_level = kFitFileLevel;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        const std::string trimmed = warptempo_parse::trim_ws(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = warptempo_parse::trim_ws(trimmed.substr(0, eq));
        const std::string value = warptempo_parse::trim_ws(trimmed.substr(eq + 1));
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

RendersettingsAuthoring read_rendersettings_authoring(
        const std::filesystem::path& path) {
    RendersettingsAuthoring out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        const std::string trimmed = warptempo_parse::trim_ws(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = warptempo_parse::trim_ws(trimmed.substr(0, eq));
        const std::string value = warptempo_parse::trim_ws(trimmed.substr(eq + 1));
        if (key == "active_tab") {
            if (value == "A") {
                out.has_active_tab = true;
                out.active_tab = 'A';
            } else if (value == "B") {
                out.has_active_tab = true;
                out.active_tab = 'B';
            }
        } else if (key == "active_audio_view") {
            if (value == "S") {
                out.has_active_audio_view = true;
                out.active_audio_view = 'S';
            } else if (value == "T") {
                out.has_active_audio_view = true;
                out.active_audio_view = 'T';
            }
        } else if (key == "trim_begin") {
            int64_t v;
            if (parse_authored_frame(value, v)) {
                out.has_trim_begin = true;
                out.trim_begin_frame = v;
            }
        } else if (key == "trim_end") {
            int64_t v;
            if (parse_authored_frame(value, v)) {
                out.has_trim_end = true;
                out.trim_end_frame = v;
            }
        } else if (key == "authoring_zoom") {
            int v;
            if (parse_int_full(value, v)) {
                out.has_zoom_level = true;
                out.zoom_level = v;
            }
        } else if (key == "authoring_viewport_start") {
            int64_t v;
            if (parse_int64_full(value, v)) {
                out.has_viewport_start = true;
                out.viewport_start = v;
            }
        } else if (key == "authoring_playhead") {
            int64_t v;
            if (parse_int64_full(value, v)) {
                out.has_playhead = true;
                out.playhead = v;
            }
        }
        // Engine-block, render-view, and unknown lines: silent-skip.
    }
    return out;
}

std::expected<EngineSettings, std::string> read_rendersettings_engine_block(
        const std::filesystem::path& path) {
    // Identical semantics to read_engine_settings_from_file: canonical
    // engine keys are read, while non-canonical keys are silently skipped.
    // read_rendersettings_view_state is the reader for the view-state side.
    return read_engine_settings_from_file(path.string());
}

bool write_rendersettings(const std::filesystem::path& path,
                           const EngineSettings& engine,
                           int64_t viewport_start,
                           int     zoom_level,
                           int64_t playhead,
                           const AuthoringSnapshot& authoring) {
    std::string data;
    append_engine_block(data, engine);
    append_view_state_block(data, viewport_start, zoom_level, playhead);
    append_authoring_block(data, authoring);
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
            const std::string trimmed = warptempo_parse::trim_ws(line);
            std::string key;
            const size_t eq = trimmed.find('=');
            if (eq != std::string::npos) {
                key = warptempo_parse::trim_ws(trimmed.substr(0, eq));
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
    defaults.title = default_render_title(stem);
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
    double font_size,
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
                std::snprintf(buf, sizeof(buf), "%.1f", playback_speed);
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
            case SettingKind::FontSizePt:
                // %g so the default round-trips as `11` (matching the
                // template) and a fractional value as e.g. `10.5`.
                std::snprintf(buf, sizeof(buf), "%g", font_size);
                data += desc.key;
                data += '=';
                data += buf;
                data += '\n';
                break;
            case SettingKind::TrimBegin_A:
                if (tab_a.trim.has_begin) {
                    data += desc.key; data += '=';
                    data += format_authored_frame(tab_a.trim.begin_frame);
                    data += '\n';
                }
                break;
            case SettingKind::TrimEnd_A:
                if (tab_a.trim.has_end) {
                    data += desc.key; data += '=';
                    data += format_authored_frame(tab_a.trim.end_frame);
                    data += '\n';
                }
                break;
            case SettingKind::TrimBegin_B:
                if (tab_b.trim.has_begin) {
                    data += desc.key; data += '=';
                    data += format_authored_frame(tab_b.trim.begin_frame);
                    data += '\n';
                }
                break;
            case SettingKind::TrimEnd_B:
                if (tab_b.trim.has_end) {
                    data += desc.key; data += '=';
                    data += format_authored_frame(tab_b.trim.end_frame);
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

    return atomic_write_string_to_path(path, data);
}
