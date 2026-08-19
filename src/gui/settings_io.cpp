#include "settings_io.h"

#include "app_state.h"
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
    PlaybackSpeedFloat,
    FollowFlag,
    GuiScalePercent,
    AudioPlayerPath,
    ProjectsRepoName,
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
    { "playback_speed",              SettingKind::PlaybackSpeedFloat,   EngineField::Title,                   "0.7" },
    { "follow",                      SettingKind::FollowFlag,           EngineField::Title,                   "true"     },
    // GUI-kind key, NOT an engine key: the GUI's rendering scale as an integer
    // percent. 100 is the design baseline (1920x1080, the supported
    // resolution); 200 is the 4K case; 50 is the half-size floor. Valid range
    // [50, 200]. Applied at load and live at the editor commit. It is THE
    // scale axis since row 7 — every
    // painted dimension in the product rides it, and the font_size key that
    // used to own the text half of that job left the schema with the monospace
    // face (architect approval 2026-08-01).
    { "gui_scale",                   SettingKind::GuiScalePercent,      EngineField::Title,                   "100"      },
    // GUI-kind launch preference, NOT an engine key: an external audio player
    // for the `l` render-listen command. Default "audacious" so the first-open
    // template writes `audio_player=audacious` (read back at load) and a fresh
    // source launches audacious on `l`; the writer always emits the line, and
    // an explicit empty value (`audio_player=`) is the deliberate opt-out
    // meaning "no external player".
    { "audio_player",                SettingKind::AudioPlayerPath,      EngineField::Title,                   "audacious" },
    // GUI-kind name of the repository holding the projects corpus — the home
    // of the architect's committed working checkpoints, and what the GitHub
    // recheck reads history out of. Placed BESIDE audio_player because the two
    // are the same shape and the same concern: free-text environment
    // preferences with no dedicated gesture, sitting at the tail of the
    // non-tab GUI band so the per-tab bands stay contiguous below.
    // The default is non-empty (unlike url/cover), and it is LITERALLY the same
    // constant both structs that carry the setting are constructed with — this
    // descriptor names kDefaultProjectsRepo (settings_file.h) rather than
    // repeating its text, so the template stamp and those defaults are one value
    // by construction and cannot drift apart with an edit to either.
    // REQUIRED LIKE EVERY OTHER KEY (architect approval 2026-08-04, retiring the
    // one-day optional-key exception): this writer always emits the line, and a
    // file lacking it is load-fatal in both products. NOT an engine key — it
    // never enters kEngineKeys and never reaches the render fingerprint.
    { "projects_repo",               SettingKind::ProjectsRepoName,     EngineField::Title,                   kDefaultProjectsRepo },
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
// state, 2026-07-30).
// audio_player's empty value is the free-text no-player opt-out. Returns
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
        case SettingKind::PlaybackSpeedFloat:
            std::snprintf(buf, sizeof(buf), "%.1f", gui.playback_speed);
            return std::string(buf);
        case SettingKind::FollowFlag:
            return std::string(gui.follow ? "true" : "false");
        case SettingKind::GuiScalePercent:
            // Plain digits, matching the canonical integer spelling
            // validate_gui_setting accepts (parse_authored_frame): the default
            // round-trips as `100`.
            std::snprintf(buf, sizeof(buf), "%d", gui.gui_scale);
            return std::string(buf);
        case SettingKind::AudioPlayerPath:
            return gui.audio_player;
        case SettingKind::ProjectsRepoName:
            // Free text, emitted verbatim in UTF-8 exactly as audio_player is.
            return gui.projects_repo;
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

bool create_if_missing(const std::filesystem::path& p,
                       const std::string& contents) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return true;
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

ViewState view_state_from_settings_tab(const SettingsFileTab& t) {
    ViewState v;
    v.viewport_start_sample  = t.viewport_start;
    v.zoom_level             = t.zoom;
    v.playhead_cursor_sample = t.playhead;
    v.read_only              = t.read_only;
    // Both bounds are always meaningful in the schema and in the store, so the
    // pair copies straight across (the unset mapping died 2026-07-30).
    v.trim.begin_frame = t.trim.begin_frame;
    v.trim.end_frame   = t.trim.end_frame;
    // THE ONE FIELD WITH NO ASSIGNMENT ABOVE IS DELIBERATE: zoom_recall_level,
    // bare `0`'s per-session return level, has no `.settings` key to read and
    // rests at the fresh struct's EMPTY here — so a load in place, whose whole
    // job is this builder's return value, drops the stamp and leaves the next
    // `0` at the ceiling meaning plain `c`. That is the ruling (architect
    // 2026-08-18: a load in place is a discontinuity, and the stamped level
    // described the piece being replaced), and it needs no reset of its own.
    // The field's own note (app_state.h) is authoritative.
    return v;
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
        app.playback_speed, app.gui_scale, app.audio_player,
        app.projects_repo};
    return format_nonengine_value(desc->kind, gui);
}
