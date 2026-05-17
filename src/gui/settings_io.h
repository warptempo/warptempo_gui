#pragma once

#include "engine_settings.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

struct ViewState;

// Parsed view-state contents of .settings. Engine-key lines are handled
// separately by read_engine_settings_from_file; this struct carries only
// the typed view-state fields (per-tab viewport / zoom / playhead, follow,
// active_audio_view, active_markers_view, active_tab_view, playback_speed,
// per-tab and legacy trim).
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
    // Legacy singleton trim keys. Accepted on read for back-compat;
    // no longer written. When present without any per-tab key they
    // apply to tab_a only.
    bool    has_trim_begin     = false;
    double  trim_begin         = 0.0;   // seconds
    bool    has_trim_end       = false;
    double  trim_end           = 0.0;   // seconds
    // Per-tab trim keys. Take precedence over the legacy singletons
    // when both shapes appear in the same file.
    bool    has_tab_a_trim_begin = false;
    double  tab_a_trim_begin     = 0.0;
    bool    has_tab_a_trim_end   = false;
    double  tab_a_trim_end       = 0.0;
    bool    has_tab_b_trim_begin = false;
    double  tab_b_trim_begin     = 0.0;
    bool    has_tab_b_trim_end   = false;
    double  tab_b_trim_end       = 0.0;
};

// Strict shape validator for MM:SS.mmm settings timestamps. Exposed so
// the settings editor's commit() can reuse the same parsing predicate
// it routes through on file load.
bool is_settings_timestamp(const std::string& s);

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

// True iff `key` is one of the ten canonical engine setting keys.
// Driven from the EnginePassthrough subset of kSettingsOrder.
bool is_canonical_engine_key(const std::string& key);

// Validate (key, value) per the canonical engine rules and assign to the
// corresponding EngineSettings field on success. On failure, leaves `out`
// untouched and fills `reason` with a short human constraint string
// (e.g. "must be one of {wav, timemap, tempomap}"). Caller wraps with
// the surrounding "key 'X' has invalid value 'Y':" prefix. Used by both
// read_engine_settings_from_file and GuiSettingsEditor::commit.
//
// Returns false with reason "unknown engine key" if `key` is not in
// the canonical engine set — defensible against callers that didn't
// pre-gate on is_canonical_engine_key.
bool validate_engine_setting(const std::string& key,
                              const std::string& value,
                              EngineSettings& out,
                              std::string& reason);

// Strict deserializer. Walks `path` looking for canonical engine-key
// lines and returns the populated typed struct. On any violation
// (unknown key, duplicate, parse failure, missing required key, file
// not openable) returns std::nullopt and logs every violation to
// stderr as `warptempo_gui: engine settings rejected: <reason>`.
//
// Non-engine canonical lines (view-state keys, follow, active_markers_view,
// playback_speed, trim variants) are ignored by this function;
// parse_settings_file handles them.
std::optional<EngineSettings> read_engine_settings_from_file(
    const std::string& path);

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
// View-state lines (viewport_start, zoom, playhead) are skipped.
// Any other unknown key, malformed value, missing required key, or
// duplicate → nullopt with a stderr line per violation.
std::optional<EngineSettings> read_rendersettings_engine_block(
    const std::filesystem::path& path);

// Full-write of `.rendersettings`: emits the ten canonical engine keys
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
// the explicit parameters; per-tab trims are emitted only when the
// corresponding has_trim_* flag is set. Matches the `.warpmarkers` write
// pattern (tmp → fsync → rename). Best-effort: failure is logged by
// the caller.
bool write_settings_file(
    const std::string& path,
    const ViewState& tab_a,
    const ViewState& tab_b,
    bool follow,
    char active_audio_view,
    char active_markers_view,
    char active_tab_view,
    float playback_speed,
    const EngineSettings& engine);
