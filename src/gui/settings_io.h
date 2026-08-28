#pragma once

#include "engine_settings.h"
#include "settings_file.h"
#include "device_config.h"   // format_gui_scale_percent (the recall)

#include <cstdint>
#include <expected>
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

// SIDECAR PRESENCE IS ONE PREDICATE (the ONE owner). "Present" is EXISTS —
// not "is a regular file": the load skips its template creation for anything
// standing at a sidecar's name and then hands that name to the strict reader,
// so a directory or a socket wearing `<stem>.settings` is a PARSE FAILURE and
// not an absence, and the answer has to be the same on both roads that ask
// (the real load's create_if_missing below, and source_load_dry_run's
// pre-flight, file_loader.h) or the dry-run would approve a reopen the load
// then refuses. A stat that FAILS is neither present nor absent: it answers
// with the system's own words, never a silent "absent".
std::expected<bool, std::string> sidecar_present(
    const std::filesystem::path& p);

// Ensure `p` exists with `contents`. If the file already exists, leave it
// alone. Returns true on success or if file already exists. Failures are
// non-fatal — the audio load still proceeds: a stat that fails is reported and
// nothing is written, and the strict reader that follows fails on the same
// name with its own words.
bool create_if_missing(const std::filesystem::path& p,
                       const std::string& contents);

// First-open default `.settings` template. Built by walking the same
// canonical key list write_settings_file walks, so the template is
// byte-identical to a save with a default-constructed EngineSettings
// (title overridden to `<stem>-rendered`), an all-zero ViewState, and the FULL
// trim window on both tabs. `total_frames` is the just-loaded source's frame
// count: the full window is [0, total-1], which is not a compile-time constant,
// so the four trim keys are the template's ONE dynamic stamp — every other
// non-engine key carries a fixed descriptor default (the `-1` unset spelling the
// trim keys used to carry died with the unset state 2026-07-30 — a template
// still writing it would no longer load).
std::string format_default_settings_template(const std::string& stem,
                                             int64_t total_frames);

// The complete non-engine (GUI-kind) value set the settings writer
// serializes, gathered into one snapshot. Constructed at each call site and
// consumed within the call: the reference members borrow the caller's
// storage (the two tab bands), the scalars are copied. One struct so the
// writer and the autocomplete recall (format_nonengine_value in
// settings_io.cpp) take the identical value set without a positional parameter
// list.
struct NonEngineSettingsSnapshot {
    const ViewState&   tab_a;
    const ViewState&   tab_b;
    bool               follow;
    char               active_audio_view;
    char               active_markers_view;
    char               active_tab_view;
    // The waveform PICTURE's magnification LEVEL — a count of doublings in
    // the range settings_file.h owns, whose gain render.h derives. A display
    // preference, but a per-PIECE one: how loud the picture wants to be drawn
    // is a fact about the material, which is why it stayed in the sidecar when
    // gui_scale left it 2026-08-27. It scales no audio anywhere.
    int                waveform_magnification_level;
    // (`projects_repo` LEFT THIS SNAPSHOT 2026-08-27 with its key — the
    // repository is the device config's, device_config.h; the sidecar carries
    // exactly what is about the piece.)
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

// The `.settings` file's exact bytes for this value set, built and returned
// without touching disk — the string half write_settings_file hands to the
// atomic writer, so the two can never diverge. Its other consumer is the
// GitHub recheck's "now" side (history_diff.h), which diffs the live state
// against a committed snapshot and needs precisely what a Ctrl+S would land at
// this instant, with no file anywhere. The key order and per-key value
// serialization are documented at the definition's kSettingsOrder walk.
std::string format_settings_text(
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
//
// THE DEVICE CONFIG'S THREE EDITABLE KEYS RECALL HERE TOO, off the same live
// AppState, even though they left the `.settings` schema 2026-08-27: the
// settings editor is still their authoring surface — the Settings dropdown's
// "GUI scale" item prefills through this very call — so a recall that answered
// nothing for them would have broken the menu item and the Tab completion
// together. What they recall is byte-identical to what the device config file
// carries, through that file's own serializer (format_gui_scale_percent,
// device_config.h) or verbatim for the two free-text keys: the same "recall and
// the file can never diverge" rule the `.settings` keys keep, only against a
// different file. (`projects_path` and `last_project` are not editable here
// and recall nothing: the first is hand-edited in the file, the second is the
// program's own.)
std::optional<std::string> recall_gui_setting_value(const AppState& app,
                                                    const std::string& key);
