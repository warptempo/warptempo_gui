#pragma once

#include "engine_settings.h"
#include "settings_file.h"
#include "device_config.h"   // format_gui_scale_percent (the recall)
#include "failure.h"
#include "sidecar_set.h"     // the sidecar list and the required-file core,
                             // parser-domain

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
// SidecarSetPresence and the all-or-nothing core, all there so the CLI and
// the render-entry load compile the same preflight). "Present"
// is EXISTS, for the reason stated there. What lives HERE is the GUI's
// GuiFailure-composing wrapper of each: a stat that fails answers with the
// system's own words, never a silent "absent" — as the TWO CLAUSES of a
// GuiFailure (failure.h), the full path on the diagnostic for the stderr line
// and the file named the basename rule's way (the project folder and the file,
// shown_project_path) on the display, because the dry run hands that clause to
// a notification card (the basename rule, failure.h). Its own caller is create_if_missing's
// belt; the set walk below does not go through it (it calls the core
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
// non-engine key carries a fixed descriptor default (the trim window has no
// unset spelling, 2026-07-30).
std::string format_default_settings_template(const std::string& stem,
                                             int64_t total_frames);

// The complete non-engine (GUI-kind) value set the settings writer
// serializes, gathered into one snapshot. Constructed at each call site and
// consumed within the call: the reference members borrow the caller's
// storage (the two tabs' view states), the scalars are copied. One struct so the
// writer and the autocomplete recall (format_nonengine_value in
// settings_io.cpp) take the identical value set without a positional parameter
// list.
struct NonEngineSettingsSnapshot {
    const ViewState&   tab_a;
    const ViewState&   tab_b;
    char               active_audio_view;
    char               active_markers_view;
    char               active_tab_view;
    // (The sidecar carries exactly what is about the piece: the repository
    // is the device config's, device_config.h; the camera postures are what
    // the user is DOING, not what the piece determines, so they are session
    // state in AppState and nothing serializes them; the waveform's gain
    // varies over source time, never a settings field.)
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

// THE DEVICE CONFIG'S EDITABLE KEYS' RECALL, off the live AppState and the
// live struct it points at: the settings editor is their authoring surface —
// the settings page's rows, the value list's seat, the line editor's prefill
// and its Tab on an empty entry all read through this very call
// (GuiSettingsEditor::setting_value). Returns std::nullopt for every other
// key (the editor falls back to format_engine_setting_value for the piece's
// engine keys and to recall_score_viewer_value for `score_path`). THE
// `.settings` FILE'S GUI-KIND KEYS — the view keys and the per-tab view
// state — RECALL NOTHING HERE (architect 2026-11-19: "One, agree."):
// no settings page row opens them and the line editor's key is its row's,
// so nothing asks; their bytes are format_nonengine_value's, the writer's.
// What the device keys recall is byte-identical to what the device
// config file carries, through that file's own serializers
// (format_gui_scale_percent, device_config.h)
// or verbatim for the two
// free-text keys (`projects_repo`, and `projects_path`, 2026-09-02),
// or the switch's word (`true_colors`, 2026-10-21), or the
// plate's grow (`waveform_grow`, 2026-10-31, through format_waveform_grow),
// or the noise's folder name verbatim (`noise`, 2026-10-28), its fixed
// attenuation (`noise_attenuation_level`, 2026-11-05 night, through
// format_level_db) and its crossfade
// (`noise_crossfade_s`, 2026-10-28, through format_noise_crossfade_s), so
// the recall and the file can never diverge.
// (`last_project` is not editable and recalls nothing: it is
// the program's own; nor does the level, `noise_level` (2026-11-05 night):
// set on the top icon row's NOISE case alone, kSettingsEditorKeys.)
std::optional<std::string> recall_gui_setting_value(const AppState& app,
                                                    const std::string& key);
