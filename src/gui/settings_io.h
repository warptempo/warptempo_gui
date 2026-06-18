#pragma once

#include "engine_settings.h"
#include "settings_trim.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

struct ViewState;

// Parsed view-state contents of .settings. Engine-key lines are handled
// separately by read_engine_settings_from_file; this struct carries only
// the typed view-state fields (per-tab viewport / zoom / playhead, follow,
// active_audio_view, active_markers_view, active_tab_view, playback_speed,
// project-level trim).
struct ParsedSettings {
    bool    has_tab_a_vp   = false;
    int64_t tab_a_vp       = 0;
    bool    has_tab_a_zoom = false;
    int     tab_a_zoom     = 0;
    bool    has_tab_a_ph   = false;
    int64_t tab_a_ph       = 0;
    bool    has_tab_b_vp   = false;
    int64_t tab_b_vp       = 0;
    bool    has_tab_b_zoom = false;
    int     tab_b_zoom     = 0;
    bool    has_tab_b_ph   = false;
    int64_t tab_b_ph       = 0;
    bool    has_follow         = false;
    bool    follow             = true;
    bool    has_active_audio_view  = false;
    char    active_audio_view      = 'S';
    bool    has_active_markers_view    = false;
    char    active_markers_view        = 'W';
    bool    has_active_tab_view    = false;
    char    active_tab_view        = 'A';
    bool    has_playback_speed = false;
    float   playback_speed     = 1.0f;
    // Project-level trim keys (trim_begin / trim_end). A single trim per
    // project; absence means unset.
    bool    has_trim_begin     = false;
    double  trim_begin         = 0.0;   // seconds
    bool    has_trim_end       = false;
    double  trim_end           = 0.0;   // seconds
    // Per-tab read-only flags. Absent → tab defaults to editable.
    bool    has_tab_a_read_only  = false;
    bool    tab_a_read_only      = false;
    bool    has_tab_b_read_only  = false;
    bool    tab_b_read_only      = false;
};

// Atomic write: tmp + fsync + rename, preserving the existing file's
// permission bits when present (0644 fallback). Returns false on any I/O
// failure, removing the partial `.tmp` first. Shared by the .settings,
// .rendersettings, .warpmarkers, and .phaseresetmarkers writers.
bool atomic_write_string_to_path(const std::string& path,
                                 const std::string& data);

// Ensure `p` exists with `contents`. If the file already exists, leave it
// alone. Returns true on success or if file already exists. Failures are
// non-fatal — the audio load still proceeds.
bool create_if_missing(const std::filesystem::path& p,
                       const std::string& contents);

// Parse `.settings`. Missing file → empty result (all has_* false).
// Returns false only on a file-open failure of an existing file; per-line
// errors are silent-skip. Tab values are stored raw, without range
// validation — the caller clamps against the current audio file. Engine
// keys are ignored by this function; they are deserialized separately by
// read_engine_settings_from_file.
bool parse_settings_file(const std::string& path, ParsedSettings& out);

// Tolerant view-state contents of `.rendersettings`. Missing keys are
// silently defaulted (fit-file zoom for `zoom`, zero for viewport/
// playhead); malformed values are silent-skipped. Engine-block lines
// in the same file are skipped — read_rendersettings_engine_block is
// the strict reader for that side.
struct RenderViewState {
    int     zoom_level     = 0;   // Filled with kFitFileLevel by the reader.
    int64_t viewport_start = 0;
    int64_t playhead       = 0;
};

// Tolerant view-state reader. Missing file → fit-file zoom + zero
// viewport + zero playhead. Engine-block lines and unknown keys are
// silent-skipped. Same tolerance as the prior
// GuiRenderView::apply_rendersettings_for.
RenderViewState read_rendersettings_view_state(
    const std::filesystem::path& path);

// Strict engine-block reader for `.rendersettings`. Same per-field
// validator and same stderr line shape as read_engine_settings_from_file.
// Non-engine lines (the view-state keys and any unknown key) are ignored.
// Only a duplicate engine key, an invalid engine value, a missing required
// key, or an unopenable file → nullopt with a stderr line per violation.
std::optional<EngineSettings> read_rendersettings_engine_block(
    const std::filesystem::path& path);

// Full-write of `.rendersettings`: emits the seven canonical engine keys
// (in kSettingsOrder order, byte-identical to the engine block of
// write_settings_file), then the three view-state keys
// (viewport_start, zoom, playhead). Atomic via tmp + fsync + rename.
// Called by the render pipeline at render time.
bool write_rendersettings(const std::filesystem::path& path,
                          const EngineSettings& engine,
                          int64_t viewport_start,
                          int     zoom_level,
                          int64_t playhead);

// View-state-only update of `.rendersettings`: read-modify-write.
// Preserves every non-view-state line in its existing order (the
// engine block + any unknown lines), replaces the three view-state
// lines (or appends them if absent) at the file's tail. Atomic via
// tmp + fsync + rename.
//
// If the file is missing, emits a view-state-only file (no engine
// block) after logging once — a later strict engine-block read on
// the same path will fail its missing-required-key checks.
bool update_rendersettings_view_state(const std::filesystem::path& path,
                                       int64_t viewport_start,
                                       int     zoom_level,
                                       int64_t playhead);

// First-open default `.settings` template. Built by walking the same
// canonical key list write_settings_file walks, so the template is
// byte-identical to a save with a default-constructed EngineSettings
// (title overridden to `<stem>-rendered`), all-zero ViewState, and no
// trims set.
std::string format_default_settings_template(const std::string& stem);

// Atomic write: emits keys in the canonical order defined by the shared
// in-file descriptor list. Engine keys are formatted from the typed
// EngineSettings parameter via per-field switch; typed scalars come from
// the explicit parameters; the project trim lines are emitted only when the
// corresponding has_trim_* flag is set. Matches the `.warpmarkers` write
// pattern (tmp → fsync → rename). Best-effort: failure is logged by
// the caller.
bool write_settings_file(
    const std::string& path,
    const ViewState& tab_a,
    const ViewState& tab_b,
    bool has_trim_begin,
    double trim_begin_seconds,
    bool has_trim_end,
    double trim_end_seconds,
    bool follow,
    char active_audio_view,
    char active_markers_view,
    char active_tab_view,
    float playback_speed,
    const EngineSettings& engine);
