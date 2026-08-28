#include "settings_io.h"

#include "app_state.h"
#include "device_config.h"
#include "frame_format.h"
#include "value_format.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <optional>
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
    FollowFlag,
    WaveformMagnificationLevel,
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
    // default": the four TRIM kinds stamp the FULL WINDOW for the just-loaded
    // source ([0, total-1] — the trim window is always set since 2026-07-30,
    // and the source's frame count is not a compile-time
    // constant). Every other non-engine descriptor carries a fixed default. For
    // engine kinds, the template default comes from a default-constructed
    // EngineSettings and this field is unused.
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
    { "follow",                      SettingKind::FollowFlag,           EngineField::Title,                   "true"     },
    // (FOUR DESCRIPTORS LEFT THIS TABLE 2026-08-27 with their keys —
    // `playback_speed` retired whole; `gui_scale`, `audio_player` and
    // `projects_repo` moved to the per-device config, device_config.h. The
    // parser-side record of all four, and the consequence for a sidecar still
    // carrying one, is at kCanonicalSettingsKeys, settings_file.cpp.)
    // GUI-kind key, NOT an engine key: the WAVEFORM PICTURE's magnification
    // LEVEL, a count of doublings in the range settings_file.h owns for both
    // products. 0 is the untouched picture and the template's stamp. The
    // gain it stands for multiplies the peaks the painter maps to rows and
    // NOTHING ELSE — no sample, no playback path, no render input — so it never
    // enters kEngineKeys and never reaches the render fingerprint. It sat
    // immediately after gui_scale while that key was here, the two being the
    // same kind of thing to look at; they parted 2026-08-27 on the question
    // this one answers differently — how loud to draw THIS material is the
    // piece's business, how big to draw the whole GUI is the panel's.
    // (architect approval 2026-08-26 for the schema addition and for the
    // same-day retune that renamed it; the parser-side record is at
    // kCanonicalSettingsKeys.)
    { "waveform_magnification_level",SettingKind::WaveformMagnificationLevel, EngineField::Title,            "0"        },
    { "tab_a_trim_begin",            SettingKind::TrimBegin_A,          EngineField::Title,                   nullptr },
    { "tab_a_trim_end",              SettingKind::TrimEnd_A,            EngineField::Title,                   nullptr },
    { "tab_a_read_only",             SettingKind::ReadOnly_A,           EngineField::Title,                   "false" },
    { "tab_a_viewport_start",        SettingKind::ViewportStart_A,      EngineField::Title,                   "0" },
    { "tab_a_zoom",                  SettingKind::ZoomLevel_A,          EngineField::Title,                   "2" },
    { "tab_a_playhead_cursor",       SettingKind::Playhead_A,           EngineField::Title,                   "0" },
    { "tab_b_trim_begin",            SettingKind::TrimBegin_B,          EngineField::Title,                   nullptr },
    { "tab_b_trim_end",              SettingKind::TrimEnd_B,            EngineField::Title,                   nullptr },
    { "tab_b_read_only",             SettingKind::ReadOnly_B,           EngineField::Title,                   "false" },
    { "tab_b_viewport_start",        SettingKind::ViewportStart_B,      EngineField::Title,                   "0" },
    { "tab_b_zoom",                  SettingKind::ZoomLevel_B,          EngineField::Title,                   "2" },
    { "tab_b_playhead_cursor",       SettingKind::Playhead_B,           EngineField::Title,                   "0" },
};

// The on-disk value text for one non-engine descriptor kind, given the
// writer's live inputs (the NonEngineSettingsSnapshot the caller built). The
// single byte definition for GUI-kind value serialization:
// write_settings_file emits `key=<this>` and the settings editor's
// autocomplete recall (recall_gui_setting_value) reads it back, so the two
// can never diverge. Every key emits a concrete value — the trim bounds
// ALWAYS write actual frames now (the `-1` unset spelling died with the unset
// state, 2026-07-30). Returns
// std::nullopt only for EnginePassthrough (engine kinds serialize through
// format_engine_field_value).
std::optional<std::string> format_nonengine_value(
        SettingKind kind,
        const NonEngineSettingsSnapshot& gui) {
    char buf[64];
    switch (kind) {
        case SettingKind::EnginePassthrough:
            return std::nullopt;
        case SettingKind::ActiveAudioViewChar:
            return std::string(1, gui.active_audio_view);
        case SettingKind::ActiveMarkersViewChar:
            return std::string(1, gui.active_markers_view);
        case SettingKind::ActiveTabViewChar:
            return std::string(1, gui.active_tab_view);
        case SettingKind::FollowFlag:
            return std::string(gui.follow ? "true" : "false");
        case SettingKind::WaveformMagnificationLevel:
            // Plain digits, the one canonical spelling validate_gui_setting's
            // range arm accepts (parse_authored_frame): the default
            // round-trips as `0`.
            std::snprintf(buf, sizeof(buf), "%d", gui.waveform_magnification_level);
            return std::string(buf);
        case SettingKind::TrimBegin_A:
            return format_authored_frame(gui.tab_a.trim.begin_frame);
        case SettingKind::TrimEnd_A:
            return format_authored_frame(gui.tab_a.trim.end_frame);
        case SettingKind::TrimBegin_B:
            return format_authored_frame(gui.tab_b.trim.begin_frame);
        case SettingKind::TrimEnd_B:
            return format_authored_frame(gui.tab_b.trim.end_frame);
        case SettingKind::ReadOnly_A:
            return std::string(gui.tab_a.read_only ? "true" : "false");
        case SettingKind::ReadOnly_B:
            return std::string(gui.tab_b.read_only ? "true" : "false");
        case SettingKind::ViewportStart_A:
            std::snprintf(buf, sizeof(buf), "%lld",
                          static_cast<long long>(gui.tab_a.viewport_start_sample));
            return std::string(buf);
        case SettingKind::ZoomLevel_A:
            // Zoom is a real-valued exponent over the one continuous domain
            // [kMinZoom, kMaxZoom]: the min-0 shortest round-trip double form
            // (format_value_double(v, 0)) is the ONE loadable spelling, matched
            // byte-for-byte by validate_gui_setting's zoom arm. An integer rest
            // (1..17) writes as its plain digits.
            return format_value_double(gui.tab_a.zoom_level, 0);
        case SettingKind::Playhead_A:
            std::snprintf(buf, sizeof(buf), "%lld",
                          static_cast<long long>(gui.tab_a.playhead_cursor_sample));
            return std::string(buf);
        case SettingKind::ViewportStart_B:
            std::snprintf(buf, sizeof(buf), "%lld",
                          static_cast<long long>(gui.tab_b.viewport_start_sample));
            return std::string(buf);
        case SettingKind::ZoomLevel_B:
            return format_value_double(gui.tab_b.zoom_level, 0);
        case SettingKind::Playhead_B:
            std::snprintf(buf, sizeof(buf), "%lld",
                          static_cast<long long>(gui.tab_b.playhead_cursor_sample));
            return std::string(buf);
    }
    return std::nullopt;
}

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

std::expected<bool, std::string> sidecar_present(
        const std::filesystem::path& p) {
    std::error_code ec;
    const bool here = std::filesystem::exists(p, ec);
    if (ec) {
        return std::unexpected("Cannot read '" + p.string() + "': " +
                               ec.message());
    }
    return here;
}

bool create_if_missing(const std::filesystem::path& p,
                       const std::string& contents) {
    auto here = sidecar_present(p);
    if (!here) {
        // The stat itself failed, so this road knows neither that the file is
        // there nor that it is not; writing a template over an unreadable name
        // is the one thing that could destroy something. Say so and write
        // nothing — the strict reader that follows fails on the same name.
        std::fprintf(stderr, "warptempo_gui: %s\n", here.error().c_str());
        return false;
    }
    if (*here) return true;
    // First-open templates are written atomically so a crash cannot leave a
    // torn file that blocks future strict loads.
    if (!atomic_write_string_to_path(p.string(), contents)) {
        std::fprintf(stderr,
                     "warptempo_gui: Could not create '%s'\n",
                     p.string().c_str());
        return false;
    }
    return true;
}

std::string format_default_settings_template(const std::string& stem,
                                             int64_t total_frames) {
    EngineSettings defaults{};
    defaults.title = default_render_title(stem);
    // First-open trim stamp: BOTH tabs get the FULL window [0, total-1] for the
    // just-loaded source — the trim window is always set (2026-07-30), and the
    // full window is exactly what the retired `-1` unset spelling used to mean.
    // The pair comes from the one seeding owner (full_trim_window, app_state.h)
    // so the template and the load reset can never disagree; `total_frames` is
    // the caller's loaded source total, which is why this is a dynamic stamp
    // and not a fixed descriptor default.
    const TrimState full = full_trim_window(total_frames);
    const std::string trim_begin_text = format_authored_frame(full.begin_frame);
    const std::string trim_end_text   = format_authored_frame(full.end_frame);
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
        } else {
            // The nullptr-default descriptors are exactly the four trim kinds
            // — the one dynamic stamp; every other non-engine descriptor
            // carries a fixed default above.
            const std::string* sv = nullptr;
            switch (desc.kind) {
                case SettingKind::TrimBegin_A:
                case SettingKind::TrimBegin_B:      sv = &trim_begin_text; break;
                case SettingKind::TrimEnd_A:
                case SettingKind::TrimEnd_B:        sv = &trim_end_text; break;
                default: break;
            }
            if (sv != nullptr) {
                s += desc.key;
                s += '=';
                s += *sv;
                s += '\n';
            }
        }
    }
    return s;
}

std::string format_settings_text(
    const NonEngineSettingsSnapshot& gui,
    const EngineSettings& engine) {
    std::string data;
    for (const auto& desc : kSettingsOrder) {
        if (desc.kind == SettingKind::EnginePassthrough) {
            data += desc.key;
            data += '=';
            data += format_engine_field_value(engine, desc.field);
            data += '\n';
            continue;
        }
        // Non-engine kinds share their value serialization with the settings
        // editor's autocomplete recall, and every one emits a line (the trim
        // bounds always write actual frames). nullopt is EnginePassthrough-only,
        // already handled above; the guard below is defensive.
        std::optional<std::string> v = format_nonengine_value(desc.kind, gui);
        if (!v) continue;
        data += desc.key;
        data += '=';
        data += *v;
        data += '\n';
    }

    return data;
}

bool write_settings_file(
    const std::string& path,
    const NonEngineSettingsSnapshot& gui,
    const EngineSettings& engine) {
    return atomic_write_string_to_path(path, format_settings_text(gui, engine));
}

std::optional<std::string> recall_gui_setting_value(const AppState& app,
                                                    const std::string& key) {
    // THE DEVICE CONFIG'S THREE EDITABLE KEYS, ahead of the `.settings` walk
    // because they are not in it any more (2026-08-27) and the settings editor
    // still edits them — the whole rationale is at the declaration. Each
    // recalls through the device config's own serializer or verbatim, so a
    // recall and that file agree byte for byte exactly as a `.settings` recall
    // and a Ctrl+S do.
    if (key == "gui_scale")     return format_gui_scale_percent(app.gui_scale);
    if (key == "audio_player")  return app.audio_player;
    if (key == "projects_repo") return app.projects_repo;

    const SettingDescriptor* desc = nullptr;
    for (const auto& d : kSettingsOrder) {
        if (key == d.key) { desc = &d; break; }
    }
    // Not a canonical GUI-kind key. Engine keys and unknown keys both land
    // here: the caller falls back to format_engine_setting_value for engine
    // keys and reports nothing recallable for the truly unknown.
    if (desc == nullptr || desc->kind == SettingKind::EnginePassthrough) {
        return std::nullopt;
    }

    // Recall exactly what a Ctrl+S at this instant would write. The save path
    // runs refresh_active_tab_view_from_app first, stashing the live
    // viewport / zoom / playhead / trim into the ACTIVE tab's band before the
    // writer reads it; mirror that stash onto local copies so recall and save
    // never diverge. read_only is not mirrored to a live field (it lives in
    // the band, toggled by bare `o`), so it needs no overlay.
    ViewState eff_a = app.tab_a;
    ViewState eff_b = app.tab_b;
    ViewState& eff_active = (app.active_tab_view == 'B') ? eff_b : eff_a;
    eff_active.viewport_start_sample  = app.viewport_start_sample;
    eff_active.zoom_level             = app.zoom_level;
    eff_active.playhead_cursor_sample = app.playhead_cursor_sample;
    eff_active.trim                   = app.trim;

    // desc->kind is non-EnginePassthrough here (guarded above), so
    // format_nonengine_value always yields a value — a trim bound recalls as its
    // actual frame (`tab_a_trim_begin=0`), matching what Ctrl+S writes.
    const NonEngineSettingsSnapshot gui{
        eff_a, eff_b, app.follow_mode,
        app.active_audio_view, app.active_markers_view, app.active_tab_view,
        app.waveform_magnification_level};
    return format_nonengine_value(desc->kind, gui);
}
