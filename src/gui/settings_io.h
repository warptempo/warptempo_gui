#pragma once

#include "engine_settings.h"
#include "settings_trim.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

struct AuthoringSnapshot;
struct ViewState;

// Parsed view-state contents of .settings. Engine-key lines are handled
// separately by read_engine_settings_from_file; this struct carries only
// the typed view-state fields (per-tab viewport / zoom / playhead / trim,
// follow, active_audio_view, active_markers_view, active_tab_view,
// playback_speed, and the GUI-kind font_size).
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
    // GUI font size in points (NOT an engine key). Absent, malformed, or
    // out-of-range values leave has_font_size false; the default (11)
    // applies at the call site.
    bool    has_font_size      = false;
    double  font_size          = 11.0;
    // Per-tab trim keys. Absence means unset.
    bool    has_tab_a_trim_begin = false;
    double  tab_a_trim_begin     = 0.0;   // seconds
    bool    has_tab_a_trim_end   = false;
    double  tab_a_trim_end       = 0.0;   // seconds
    bool    has_tab_b_trim_begin = false;
    double  tab_b_trim_begin     = 0.0;   // seconds
    bool    has_tab_b_trim_end   = false;
    double  tab_b_trim_end       = 0.0;   // seconds
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
// Returns false on a file-open failure of an existing file, or on any hard
// failure from the delegated trim reader (read_settings_trim): a malformed
// trim timestamp, a duplicate trim key, or a duplicate or non-A-or-B
// active_tab_view. Bound ordering is not checked — equal and inverted trim
// bounds load intact (the render boundary owns trim validity).
// Other per-line view-state errors are skipped, silently
// except font_size, whose bad values get one stderr diagnostic before the
// default applies. Tab values are stored raw, without range validation — the
// caller clamps against the current audio file. Engine keys are ignored by
// this function; they are deserialized separately by
// read_engine_settings_from_file.
bool parse_settings_file(const std::string& path, ParsedSettings& out);

// Tolerant view-state contents of `.rendersettings`. Missing keys are
// silently defaulted (fit-file zoom for `zoom`, zero for viewport/
// playhead); malformed values are silent-skipped. Engine-block lines
// in the same file are skipped — read_rendersettings_engine_block is
// the strict reader for that side, returning std::expected rather than
// tolerating malformed input.
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

// Tolerant authoring-block reader for .rendersettings. Every field is
// has_-flagged; missing keys leave their flag false. Older or minimal
// .rendersettings sidecars may omit the authoring block, and commit skips
// fields whose has_ flag is false.
// Malformed values are silent-skipped like the view-state reader.
struct RendersettingsAuthoring {
    bool    has_active_tab        = false;
    char    active_tab            = 'A';
    bool    has_active_audio_view = false;
    char    active_audio_view     = 'S';
    bool    has_trim_begin        = false;
    double  trim_begin_sec        = 0.0;
    bool    has_trim_end          = false;
    double  trim_end_sec          = 0.0;
    bool    has_zoom_level        = false;
    int     zoom_level            = 0;
    bool    has_viewport_start    = false;
    int64_t viewport_start        = 0;
    bool    has_playhead          = false;
    int64_t playhead              = 0;
};

RendersettingsAuthoring read_rendersettings_authoring(
    const std::filesystem::path& path);

// Strict engine-block reader for `.rendersettings`. Delegates to
// read_engine_settings_from_file, so it shares that reader's per-field
// validator and error semantics exactly. Non-engine lines (the view-state
// keys and any unknown key) are ignored. A duplicate engine key, an
// invalid engine value, a missing required key, or an unopenable file
// yields std::unexpected carrying the underlying reader's reason string
// for the FIRST such violation encountered (the reader does not continue
// past it). This function prints nothing; it performs no I/O beyond
// reading `path`. The caller owns surfacing the reason as a diagnostic.
std::expected<EngineSettings, std::string> read_rendersettings_engine_block(
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
