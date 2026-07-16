#include "settings_io.h"

#include "app_state.h"
#include "frame_format.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace {

// Canonical .settings layout. One descriptor per line in the file, in
// the exact order they appear on disk. Shared by
// format_default_settings_template (template build) and
// write_settings_file (Ctrl+S). Reading is order-insensitive —
// read_settings_file (the parser-side schema) does not consult this list.
enum class SettingKind {
    EnginePassthrough,
    ActiveAudioViewChar,
    ActiveMarkersViewChar,
    ActiveTabViewChar,
    PlaybackSpeedFloat,
    FollowFlag,
    FontSizePt,
    AudioPlayerPath,
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
    // GUI view-state band begins: non-tab keys, then all tab A keys,
    // then all tab B keys.
    { "active_audio_view",           SettingKind::ActiveAudioViewChar,  EngineField::Title,                   "S"        },
    { "active_markers_view",         SettingKind::ActiveMarkersViewChar,EngineField::Title,                   "W"        },
    { "active_tab_view",             SettingKind::ActiveTabViewChar,    EngineField::Title,                   "A"        },
    { "playback_speed",              SettingKind::PlaybackSpeedFloat,   EngineField::Title,                   "0.7" },
    { "follow",                      SettingKind::FollowFlag,           EngineField::Title,                   "true"     },
    // GUI-kind key, NOT an engine key: the single GUI-wide monospace text
    // size in points (pixels = points * 4/3). Valid range 6..72. Like
    // playback_speed, the file's value is applied once at launch when the
    // source loads.
    { "font_size",                   SettingKind::FontSizePt,           EngineField::Title,                   "11"       },
    // GUI-kind launch preference, NOT an engine key: an external audio player
    // for the `l` render-listen command. Default "audacious" so the first-open
    // template writes `audio_player=audacious` (read back at load) and a fresh
    // source launches audacious on `l`; the writer always emits the line, and
    // an explicit empty value (`audio_player=`) is the deliberate opt-out
    // meaning "no external player".
    { "audio_player",                SettingKind::AudioPlayerPath,      EngineField::Title,                   "audacious" },
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

} // namespace

// Atomic write: tmp + fsync + rename, preserving the existing file's
// permission bits when present (0644 fallback). Shared by
// write_settings_file and the warp/phase-reset marker writers.
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

ViewState view_state_from_settings_tab(const SettingsFileTab& t) {
    ViewState v;
    v.viewport_start_sample  = t.viewport_start;
    v.zoom_level             = t.zoom;
    v.playhead_cursor_sample = t.playhead;
    v.read_only              = t.read_only;
    v.trim.has_begin   = t.trim.has_begin;
    v.trim.begin_frame = t.trim.begin_frame;
    v.trim.has_end     = t.trim.has_end;
    v.trim.end_frame   = t.trim.end_frame;
    return v;
}

std::string format_default_settings_template(const std::string& stem) {
    EngineSettings defaults{};
    defaults.title = default_render_title(stem);
    std::string s;
    for (const auto& desc : kSettingsOrder) {
        if (desc.kind == SettingKind::EnginePassthrough) {
            s += desc.key;
            s += '=';
            s += format_engine_field_value(defaults, desc.field);
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
    const std::string& audio_player,
    const EngineSettings& engine) {
    std::string data;
    char buf[64];
    for (const auto& desc : kSettingsOrder) {
        switch (desc.kind) {
            case SettingKind::EnginePassthrough:
                data += desc.key;
                data += '=';
                data += format_engine_field_value(engine, desc.field);
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
            case SettingKind::AudioPlayerPath:
                // Always written: the key is present in every product-written
                // .settings. An empty value (`audio_player=`) is legal and
                // means "no external player"; the strict reader accepts it in
                // both products.
                data += desc.key;
                data += '=';
                data += audio_player;
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
