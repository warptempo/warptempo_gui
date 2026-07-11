#include "settings_io.h"

#include "app_state.h"
#include "parser/parse_text_util.h"
#include "render_pipeline.h"
#include "frame_format.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
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

// Append the engine block (the eight canonical engine keys, in
// kSettingsOrder order, byte-identical to the engine block of
// write_settings_file) to `out`. Shared by write_rendersettings.
void append_engine_block(std::string& out, const EngineSettings& engine) {
    for (const auto& desc : kSettingsOrder) {
        if (desc.kind != SettingKind::EnginePassthrough) continue;
        out += desc.key;
        out += '=';
        out += format_engine_field_value(engine, desc.field);
        out += '\n';
    }
}

// Append the three view-state keys (bare names, canonical order) to
// `out`. Consumed by write_rendersettings.
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
// write_rendersettings, write_settings_file (and through it
// update_settings_view_state), and the warp/phase-reset marker writers.
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

std::expected<Rendersettings, std::string> read_rendersettings(
        const std::filesystem::path& path) {
    Rendersettings out;
    out.view.zoom_level = kFitFileLevel;

    std::ifstream f(path);
    if (!f) {
        return std::unexpected("could not open '" + path.string() + "'");
    }

    // The authoring block is none-or-all on its five core keys. Scan into a
    // working struct plus a presence flag per authoring key, then decide the
    // block's shape once at the tail: all five core keys engage the optional,
    // none of them (and no orphan trim key) yields nullopt, anything else is
    // an adversarial partial block and refuses the whole read.
    RendersettingsAuthoring work;
    bool has_active_tab        = false;
    bool has_active_audio_view = false;
    bool has_zoom_level        = false;
    bool has_viewport_start    = false;
    bool has_playhead          = false;
    // First trim key seen in file order, for the orphan-trim refusal message.
    std::string first_trim_key;

    auto valid_zoom = [](int z) {
        return z >= kFitFileLevel && z <= kMaxNumericLevel;
    };
    auto scan = warptempo_settings::scan_settings_file(
        f, [&](int ln, const std::string& key,
               const std::string& value)
               -> std::expected<void, std::string> {
        using warptempo_settings::bad_value;

        if (auto e = warptempo_settings::try_engine_key(ln, key, value,
                                                        out.engine)) {
            return *e;
        } else if (key == "viewport_start") {
            int64_t v;
            if (!warptempo_parse::parse_int64_strict(value, v) || v < 0) {
                return bad_value(ln, key, value,
                                 "must be a non-negative integer");
            }
            out.view.viewport_start = v;
        } else if (key == "zoom") {
            int v;
            if (!warptempo_parse::parse_int_strict(value, v) ||
                !valid_zoom(v)) {
                return bad_value(ln, key, value, "must be a zoom level");
            }
            out.view.zoom_level = v;
        } else if (key == "playhead") {
            int64_t v;
            if (!warptempo_parse::parse_int64_strict(value, v) || v < 0) {
                return bad_value(ln, key, value,
                                 "must be a non-negative integer");
            }
            out.view.playhead = v;
        } else if (key == "active_tab") {
            if (value != "A" && value != "B") {
                return bad_value(ln, key, value, "must be A or B");
            }
            has_active_tab = true;
            work.active_tab = value[0];
        } else if (key == "active_audio_view") {
            if (value != "S" && value != "T") {
                return bad_value(ln, key, value, "must be S or T");
            }
            has_active_audio_view = true;
            work.active_audio_view = value[0];
        } else if (key == "trim_begin") {
            int64_t v;
            if (!parse_authored_frame(value, v)) {
                return bad_value(ln, key, value,
                                 "must be a whole source-frame position");
            }
            work.has_trim_begin = true;
            work.trim_begin_frame = v;
            if (first_trim_key.empty()) first_trim_key = "trim_begin";
        } else if (key == "trim_end") {
            int64_t v;
            if (!parse_authored_frame(value, v)) {
                return bad_value(ln, key, value,
                                 "must be a whole source-frame position");
            }
            work.has_trim_end = true;
            work.trim_end_frame = v;
            if (first_trim_key.empty()) first_trim_key = "trim_end";
        } else if (key == "authoring_zoom") {
            int v;
            if (!warptempo_parse::parse_int_strict(value, v) ||
                !valid_zoom(v)) {
                return bad_value(ln, key, value, "must be a zoom level");
            }
            has_zoom_level = true;
            work.zoom_level = v;
        } else if (key == "authoring_viewport_start") {
            int64_t v;
            if (!warptempo_parse::parse_int64_strict(value, v) || v < 0) {
                return bad_value(ln, key, value,
                                 "must be a non-negative integer");
            }
            has_viewport_start = true;
            work.viewport_start = v;
        } else if (key == "authoring_playhead") {
            int64_t v;
            if (!warptempo_parse::parse_int64_strict(value, v) || v < 0) {
                return bad_value(ln, key, value,
                                 "must be a non-negative integer");
            }
            has_playhead = true;
            work.playhead = v;
        } else {
            return warptempo_parse::prefix_line_error(
                ln, "unknown key '" + key + "'");
        }
        return {};
    });
    if (!scan) return std::unexpected(std::move(scan.error()));

    // Tail check: the block is none-or-all on the five core keys. Count them
    // in the writer's emission order so a partial block names the first
    // missing core key; a trim key with no core block is an orphan and names
    // the trim key. Both shapes are GUI-unproducible, hence adversarial.
    const int core_present = static_cast<int>(has_active_tab) +
                             static_cast<int>(has_active_audio_view) +
                             static_cast<int>(has_zoom_level) +
                             static_cast<int>(has_viewport_start) +
                             static_cast<int>(has_playhead);
    if (core_present == 5) {
        out.authoring = work;
    } else if (core_present == 0 && !work.has_trim_begin &&
               !work.has_trim_end) {
        // Fully absent block: the old-sidecar compatibility case.
    } else if (core_present == 0) {
        return std::unexpected("'" + first_trim_key +
                               "' requires the authoring block");
    } else {
        const char* missing =
            !has_active_tab        ? "active_tab" :
            !has_active_audio_view ? "active_audio_view" :
            !has_zoom_level        ? "authoring_zoom" :
            !has_viewport_start    ? "authoring_viewport_start" :
                                     "authoring_playhead";
        return std::unexpected(std::string(
            "authoring block is incomplete: missing '") + missing + "'");
    }
    return out;
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

bool update_settings_view_state(const std::filesystem::path& path,
                                int64_t viewport_start,
                                int     zoom_level,
                                int64_t playhead,
                                char    active_markers_view) {
    // Strict read-modify-write. The file is program-written, so any parse
    // failure — a missing file included — is adversarial at this mutation
    // boundary: refuse the update with one stderr line rather than
    // perpetuate malformed bytes or manufacture a view-state-only file. On
    // success, re-serialize the whole file canonically from the parsed
    // struct with the fresh view state on the tab named by the FILE's
    // active_tab_view; a strict-parsed program-written file has no unknown
    // lines or comments to lose, so every other key round-trips (absent
    // optional keys re-emit as their canonical defaults, the same values
    // the readers apply).
    auto sf = read_settings_file(path.string());
    if (!sf) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: view-state update refused for "
            "'%s': %s\n", path.string().c_str(), sf.error().c_str());
        return false;
    }

    auto to_view_state = [](const SettingsFileTab& t) {
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
    };
    ViewState tab_a = to_view_state(sf->tab_a);
    ViewState tab_b = to_view_state(sf->tab_b);
    ViewState& browsed = (sf->active_tab_view == 'B') ? tab_b : tab_a;
    browsed.viewport_start_sample  = viewport_start;
    browsed.zoom_level             = zoom_level;
    browsed.playhead_cursor_sample = playhead;

    return write_settings_file(path.string(), tab_a, tab_b, sf->follow,
                               sf->active_audio_view, active_markers_view,
                               sf->active_tab_view, sf->playback_speed,
                               sf->font_size, sf->engine);
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
