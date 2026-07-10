#pragma once

#include "engine_settings.h"
#include "settings_file.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

struct AuthoringSnapshot;
struct ViewState;

// The `.settings` reader is the parser-side whole-file schema
// (read_settings_file in settings_file.h), shared verbatim with
// warptempo_cli. This header carries the GUI-only settings surfaces: the
// writers and the `.rendersettings` schema.

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

// Whole-file strict schema for `.rendersettings`, the render-pipeline
// sibling of read_settings_file: one parse validates the complete
// program-written file and splits it into three projections — the typed
// engine block, the render-view scratch state, and the optional authoring
// snapshot. The file is program-written (write_rendersettings /
// update_rendersettings_view_state), so any violation is adversarial and
// the FIRST error refuses the whole read: unknown keys, keyless lines,
// duplicates, malformed values, out-of-range zoom levels, and missing
// required engine keys. Absent optional keys stay legal (defaults for the
// view projection, has_ flags for the authoring projection: older or
// minimal sidecars omit the authoring block, and the view-state-only file
// update_rendersettings_view_state can leave behind already fails its
// missing-engine-key check here).
//
// The two consumers apply opposite policies on a refused read, matching
// the marker display sidecars' recorded leniency split: the render-view
// DISPLAY caller logs once and falls back to defaults (view state is
// display scratch, never adopted into authoring), while Ctrl+Alt+C
// promotion aborts before its first marker or AppState mutation — a
// hand-edited sidecar must never partially restore.

struct RenderViewState {
    int     zoom_level     = 0;   // Filled with kFitFileLevel by the reader.
    int64_t viewport_start = 0;
    int64_t playhead       = 0;
};

struct RendersettingsAuthoring {
    bool    has_active_tab        = false;
    char    active_tab            = 'A';
    bool    has_active_audio_view = false;
    char    active_audio_view     = 'S';
    bool    has_trim_begin        = false;
    int64_t trim_begin_frame      = 0;     // source frames
    bool    has_trim_end          = false;
    int64_t trim_end_frame        = 0;     // source frames
    bool    has_zoom_level        = false;
    int     zoom_level            = 0;
    bool    has_viewport_start    = false;
    int64_t viewport_start        = 0;
    bool    has_playhead          = false;
    int64_t playhead              = 0;
};

struct Rendersettings {
    EngineSettings          engine;
    RenderViewState         view;
    RendersettingsAuthoring authoring;
};

std::expected<Rendersettings, std::string> read_rendersettings(
    const std::filesystem::path& path);

// Full-write of `.rendersettings`: emits the eight canonical engine keys
// (in kSettingsOrder order, byte-identical to the engine block of
// write_settings_file), then the three render-view scratch keys
// (viewport_start, zoom, playhead), then the authoring block when
// authoring.valid. Atomic via tmp + fsync + rename. Called by the render
// pipeline at render time.
bool write_rendersettings(const std::filesystem::path& path,
                          const EngineSettings& engine,
                          int64_t viewport_start,
                          int     zoom_level,
                          int64_t playhead,
                          const AuthoringSnapshot& authoring);

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
// the explicit parameters; the per-tab trim lines are emitted only when the
// corresponding trim flag is set on tab_a.trim / tab_b.trim. Matches the
// `.warpmarkers` write pattern (tmp → fsync → rename). Best-effort: failure
// is logged by the caller.
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
    const EngineSettings& engine);
