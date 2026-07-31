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
// The marker selection fields take their ViewState defaults (a parsed band
// carries no selection; trim is outside the selection system). One home for
// the band-to-ViewState field mapping; the sole caller is the `'` render-commit
// (adopt_render_entry) full-inheritance commit.
ViewState view_state_from_settings_tab(const SettingsFileTab& t);

// First-open default `.settings` template. Built by walking the same
// canonical key list write_settings_file walks, so the template is
// byte-identical to a save with a default-constructed EngineSettings
// (title overridden to `<stem>-rendered`), an all-zero ViewState, and the FULL
// trim window on both tabs. `total_frames` is the just-loaded source's frame
// count: the full window is [0, total-1], which is not a compile-time constant,
// so the trim keys are a dynamic template stamp like the four env hashes (the
// `-1` unset spelling they used to carry died with the unset state 2026-07-30 —
// a template still writing it would no longer load).
std::string format_default_settings_template(const std::string& stem,
                                             int64_t total_frames);

// The complete non-engine (GUI-kind) value set the settings writer
// serializes, gathered into one snapshot. Constructed at each call site and
// consumed within the call: the reference members borrow the caller's
// storage (the two tab bands and the audio_player string), the scalars are
// copied. One struct so the writer and the autocomplete recall
// (format_nonengine_value in settings_io.cpp) take the identical value set
// without a positional parameter list.
struct NonEngineSettingsSnapshot {
    const ViewState&   tab_a;
    const ViewState&   tab_b;
    bool               follow;
    char               active_audio_view;
    char               active_markers_view;
    char               active_tab_view;
    float              playback_speed;
    double             font_size;
    int                gui_scale;
    const std::string& audio_player;
    // The STORED render-environment hashes (AppState's four *_hash fields, or
    // the dispatch-moment copies in AuthoringSnapshot) — never the current
    // environment's: an unacknowledged mismatch must survive a save.
    const std::string& libm_hash;
    const std::string& libmvec_hash;
    const std::string& fftw3_hash;
    const std::string& fftw3_threads_hash;
};

// Atomic write: emits keys in the canonical order defined by the shared
// in-file descriptor list. Engine keys are formatted from the typed
// EngineSettings parameter via per-field switch; typed scalars come from
// the snapshot; all four per-tab trim lines (tab_a/tab_b begin/
// end) are always emitted as actual source frames — the trim window is always
// set, so there is no unset spelling to emit (2026-07-30).
// Matches the `.warpmarkers` write pattern (tmp → fsync → rename).
// Best-effort: failure is logged by the caller.
bool write_settings_file(
    const std::string& path,
    const NonEngineSettingsSnapshot& gui,
    const EngineSettings& engine);

// The on-disk value text that write_settings_file would emit for GUI-kind
// `key` given the current live AppState — byte-identical to a Ctrl+S at this
// instant (it mirrors the pre-write refresh_active_tab_view_from_app stash for
// the active tab). Shared with the writer through format_nonengine_value so
// recall and save can never diverge. Returns std::nullopt for engine keys (the
// settings editor falls back to format_engine_setting_value) and for unknown
// keys; a trim bound recalls as its actual frame, exactly as the writer emits
// it. Used by the settings prompt's Tab autocomplete.
std::optional<std::string> recall_gui_setting_value(const AppState& app,
                                                    const std::string& key);
