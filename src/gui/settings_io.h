#pragma once

#include "engine_settings.h"
#include "settings_file.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

struct ViewState;
struct AppState;

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

// Convert one parsed `.settings` tab band into a live ViewState: the
// viewport / zoom / playhead scratch, the read-only flag, and the trim pair.
// The selection and trim-selection fields take their ViewState defaults (a
// parsed band carries no selection). One home for the band-to-ViewState
// field mapping; the sole caller is the Shift+. render-commit
// (adopt_render_entry) full-inheritance commit.
ViewState view_state_from_settings_tab(const SettingsFileTab& t);

// First-open default `.settings` template. Built by walking the same
// canonical key list write_settings_file walks, so the template is
// byte-identical to a save with a default-constructed EngineSettings
// (title overridden to `<stem>-rendered`), all-zero ViewState, and no
// trims set.
std::string format_default_settings_template(const std::string& stem);

// Atomic write: emits keys in the canonical order defined by the shared
// in-file descriptor list. Engine keys are formatted from the typed
// EngineSettings parameter via per-field switch; typed scalars come from
// the explicit parameters; all four per-tab trim lines (tab_a/tab_b begin/
// end) are always emitted, `-1` when the corresponding trim flag is unset
// on tab_a.trim / tab_b.trim (the load grammar reads -1 back as unset).
// Matches the `.warpmarkers` write pattern (tmp → fsync → rename).
// Best-effort: failure is logged by the caller.
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
    const EngineSettings& engine);

// The on-disk value text that write_settings_file would emit for GUI-kind
// `key` given the current live AppState — byte-identical to a Ctrl+S at this
// instant (it mirrors the pre-write refresh_active_tab_view_from_app stash for
// the active tab). Shared with the writer through format_nonengine_value so
// recall and save can never diverge. Returns std::nullopt for engine keys (the
// settings editor falls back to format_engine_setting_value) and for unknown
// keys; an unset trim bound recalls as the literal `-1` (the same unset
// spelling the writer emits). Used by the settings prompt's Tab autocomplete.
std::optional<std::string> recall_gui_setting_value(const AppState& app,
                                                    const std::string& key);
