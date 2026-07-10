#pragma once

#include "engine_settings.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <istream>
#include <optional>
#include <string>

// Whole-file strict schema for the `.settings` sidecar — the single owner
// of key membership, duplicate detection, per-key value grammar, and the
// required/optional split, run by BOTH products (GuiFileLoader::load_file
// and warptempo_cli) before any value is applied. A sidecar set is loadable
// in the GUI and the CLI, or in neither.
//
// The file is program-written (Ctrl+S / the first-open template), so every
// violation is adversarial under the two-category rule and load-fatal with
// the FIRST error only: an unknown key, a keyless non-comment line, a
// duplicate of ANY key, a malformed or out-of-vocabulary value (an
// off-preset playback_speed included), and a missing required engine key
// (title, output_format, scale, limiter) all refuse. Blank lines and '#'
// comment lines stay skippable — the long-standing settings line grammar.
// Absent optional keys are legal (has_* stays false; callers apply their
// defaults): older sidecars carry fewer keys and remain loadable.
//
// Context-dependent range rules stay caller-side ON TOP of this schema:
// viewport/playhead bounds against the loaded audio and the zoom-level
// range are GUI-domain knowledge, checked load-fatally by the GUI after
// this reader accepts the syntax. Trim bound ordering is deliberately NOT
// checked anywhere at load — equal and inverted bounds are legal authored
// states the defect series walks; the render boundary owns trim refusals.

// The persisted zoom-level vocabulary, enforced by this schema in both
// products. kFitFileLevel = 0 ("whole file visible", computed at zoom /
// resize time, not stored as a fixed ms/pixel); numeric levels run
// kMinNumericLevel..kMaxNumericLevel inclusive, each exactly 2x the
// previous in ms-per-pixel (the table itself is GUI-side, in main.cpp;
// app_state.h derives its table size from kMaxNumericLevel). The constants
// live here rather than in the GUI so an out-of-vocabulary persisted zoom
// refuses identically in the GUI and the CLI.
constexpr int kFitFileLevel    = 0;
constexpr int kMinNumericLevel = 1;
constexpr int kMaxNumericLevel = 10;

// One tab's trim in the .settings schema. Positions are whole source frames
// (int64_t), decoded via parse_authored_frame (frame_format.h). A has_* of
// false means the key was absent; the paired _frame field must not be read.
struct SettingsTrim {
    bool    has_begin   = false;
    int64_t begin_frame = 0;
    bool    has_end     = false;
    int64_t end_frame   = 0;
};

// One tab's view-state band: viewport / zoom / playhead scratch, the
// read-only flag, and the trim pair.
struct SettingsFileTab {
    bool    has_viewport_start = false;
    int64_t viewport_start     = 0;
    bool    has_zoom           = false;
    int     zoom               = 0;
    bool    has_playhead       = false;
    int64_t playhead           = 0;
    bool    has_read_only      = false;
    bool    read_only          = false;
    SettingsTrim trim;
};

struct SettingsFile {
    // The typed engine block; the four required keys are guaranteed
    // present, the provenance keys default empty.
    EngineSettings engine;

    SettingsFileTab tab_a;
    SettingsFileTab tab_b;

    bool   has_follow              = false;
    bool   follow                  = true;
    bool   has_active_audio_view   = false;
    char   active_audio_view       = 'S';   // S | T
    bool   has_active_markers_view = false;
    char   active_markers_view     = 'W';   // W | P
    bool   has_active_tab_view     = false;
    char   active_tab_view         = 'A';   // A | B
    bool   has_playback_speed      = false;
    float  playback_speed          = 1.0f;  // preset vocabulary only
    bool   has_font_size           = false;
    double font_size               = 11.0;  // points, [6, 72]
};

// Parse and validate the whole `.settings` file at `path`. An unopenable
// file is an error (the GUI writes the template before its first read; the
// CLI checks existence with its own message first). Errors carry a
// "line N: " prefix where a line is at fault; callers add the path context.
std::expected<SettingsFile, std::string> read_settings_file(
    const std::string& path);

// Shared strict line scanner for the settings-format sidecars. Both
// whole-file readers — read_settings_file (`.settings`) and read_rendersettings
// (`.rendersettings`, GUI-side) — build on this: it owns the one lexical
// contract they hold in common, while each reader keeps its own distinct
// per-key vocabulary and validation arms. The two files' error strings are
// byte-identical because they share these functions.
namespace warptempo_settings {

// Build the "key 'K' has invalid value 'V': RULE" refusal, line-prefixed.
std::unexpected<std::string> bad_value(int ln, const std::string& key,
                                       const std::string& value,
                                       const std::string& rule);

// Per-meaningful-line callback: given the 1-based line number, key, and
// value, either accept (`return {};`) or refuse (`return bad_value(...)` /
// `return warptempo_parse::prefix_line_error(...)`).
using SettingsLineFn = std::function<std::expected<void, std::string>(
    int ln, const std::string& key, const std::string& value)>;

// Scan `in` under the shared lexical contract — BOM strip on line 1,
// whitespace trim, blank and '#' comment skip, first-'=' split, empty-key
// and keyless-line refusal, duplicate-key refusal against a seen set — and
// invoke `on_pair` once per meaningful line. After the loop, enforce the
// four required engine keys (title, output_format, scale, limiter). Returns
// the first error (lexical, callback, or missing-required-key), or success.
std::expected<void, std::string> scan_settings_file(std::istream& in,
                                                    const SettingsLineFn& on_pair);

// Shared engine-key arm. If `key` is a canonical engine key, validate
// (key, value) into `engine` and return the outcome (`{}` on accept, a
// bad_value refusal otherwise). If `key` is not an engine key, returns
// std::nullopt so the caller falls through to its own per-key arms.
std::optional<std::expected<void, std::string>> try_engine_key(
    int ln, const std::string& key, const std::string& value,
    EngineSettings& engine);

}  // namespace warptempo_settings
