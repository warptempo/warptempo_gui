#pragma once

#include "engine_settings.h"
#include "settings_file.h"
#include "device_config.h"   // format_gui_scale_percent (the recall)
#include "failure.h"
#include "sidecar_set.h"     // the sidecar list and the required-file core,
                             // parser-domain since 2026-09-16

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
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
// failure, removing the partial `.tmp` first. Shared by the three sidecar
// writers (kSidecarExtensions, sidecar_set.h).
bool atomic_write_string_to_path(const std::string& path,
                                 const std::string& data);

// SIDECAR PRESENCE IS ONE PREDICATE and it lives at the PARSER
// (sidecar_exists, sidecar_set.h — with kSidecarExtensions, kSidecarCount,
// SidecarSetPresence and the all-or-nothing core, all moved there 2026-09-16
// so the CLI and the render-entry load compile the same preflight). "Present"
// is EXISTS, for the reason stated there. What lives HERE is the GUI's
// GuiFailure-composing wrapper of each: a stat that fails answers with the
// system's own words, never a silent "absent" — as the TWO CLAUSES of a
// GuiFailure (failure.h), the full path on the diagnostic for the stderr line
// and the file named the basename rule's way (the project folder and the file,
// shown_project_path) on the display, because the dry run hands that clause to
// a notification card (messaging.md). Its own caller is create_if_missing's
// belt; the set walk below no longer goes through it (it calls the core
// directly and composes the same two clauses from the core's defect).
std::expected<bool, GuiFailure> sidecar_present(
    const std::filesystem::path& p);

// THE REQUIRED-FILE RULE'S GUI FACE — sidecar_set_presence_core (sidecar_set.h,
// where the rule itself is stated) with its defect composed into a GuiFailure:
// SOME AND NOT ALL is "Missing '<file>'" naming the first absent member in
// kSidecarExtensions order, an unreadable name is "Cannot read '<file>':
// <words>", and nothing is written in any case; a file beside the source that
// is no member of the set is never asked (sidecar_set.h). Read by BOTH GUI roads that
// must agree — the real load (GuiFileLoader::load_file, where `None` means a
// NEW project and writes the three templates, and a refusal is fatal) and its
// strict preview (source_load_dry_run, where the refusal is the picker's and
// Revert's card). THE OTHER TWO ROADS ASK THE CORE DIRECTLY, each composing
// its own surface's sentence: the CLI (cli_main.cpp) and the render player's
// load in place (load_render_entry_in_place, input_key_dispatch.cpp), neither
// of which authors a template, so `None` refuses there.
std::expected<SidecarSetPresence, GuiFailure> sidecar_set_presence(
    const std::filesystem::path& parent, const std::string& stem);

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
    char               active_audio_view;
    char               active_markers_view;
    char               active_tab_view;
    // (`projects_repo` LEFT THIS SNAPSHOT 2026-08-27 with its key — the
    // repository is the device config's, device_config.h; the sidecar carries
    // exactly what is about the piece. `follow`, `centered` and
    // `center_on_next_marker` left it 2026-09-11 with theirs — the three
    // camera postures are what the user is DOING, not what the piece
    // determines, so they became session state in AppState (the third
    // deleted whole 2026-09-13) and nothing serializes them.
    // The waveform magnification level left it 2026-09-14 with its key — the
    // picture's gain varies over source time, never a settings field.)
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
// THE DEVICE CONFIG'S FIVE EDITABLE KEYS RECALL HERE TOO, off the live
// AppState and the live struct it points at, even though they are not in the
// `.settings` schema: the settings editor is their authoring surface — the
// Settings dropdown's five device rows prefill through this very call — so a
// recall that answered nothing for them would break the menu rows and the Tab
// completion together. What they recall is byte-identical to what the device
// config file carries, through that file's own serializers
// (format_gui_scale_percent and format_max_waveform_height, device_config.h)
// or verbatim for the three
// free-text keys (`projects_repo`, and since 2026-09-02 `projects_path` and
// `sync_path`): the same "recall and the file can never diverge" rule the
// `.settings` keys keep, only against a different file. (`last_project` is
// not editable and recalls nothing: it is the program's own.)
std::optional<std::string> recall_gui_setting_value(const AppState& app,
                                                    const std::string& key);
