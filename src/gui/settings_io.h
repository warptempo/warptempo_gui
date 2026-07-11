#pragma once

#include "engine_settings.h"
#include "settings_file.h"

#include <cstdint>
#include <filesystem>
#include <string>

struct ViewState;

// The `.settings` reader is the parser-side whole-file schema
// (read_settings_file in settings_file.h), shared verbatim with
// warptempo_cli. This header carries the GUI-only settings surfaces: the
// writers.

// Atomic write: tmp + fsync + rename, preserving the existing file's
// permission bits when present (0644 fallback). Returns false on any I/O
// failure, removing the partial `.tmp` first. Shared by the .settings,
// .warpmarkers, and .phaseresetmarkers writers.
bool atomic_write_string_to_path(const std::string& path,
                                 const std::string& data);

// Ensure `p` exists with `contents`. If the file already exists, leave it
// alone. Returns true on success or if file already exists. Failures are
// non-fatal — the audio load still proceeds.
bool create_if_missing(const std::filesystem::path& p,
                       const std::string& contents);

// View-state-only update of a `.settings` file: strict read-modify-write,
// the per-entry autosave render view runs at its navigation/exit
// boundaries. Strict-reads the existing file through read_settings_file,
// then re-serializes the whole file canonically via write_settings_file
// with viewport_start / zoom_level / playhead updated on the tab named by
// the FILE's active_tab_view and active_markers_view replaced by the
// browsed value; every other key is preserved from the parse. Atomic via
// tmp + fsync + rename.
//
// Any read failure — a missing file included — refuses the update: one
// stderr line and a false return, no write. The file is program-written,
// so a refused read means adversarial or absent bytes this mutation
// boundary will not perpetuate or manufacture. Callers already tolerate
// false.
bool update_settings_view_state(const std::filesystem::path& path,
                                int64_t viewport_start,
                                int     zoom_level,
                                int64_t playhead,
                                char    active_markers_view);

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
