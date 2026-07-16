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
// off-preset playback_speed included), and a missing canonical key all
// refuse. Lexing is byte-exact: each line is split at its first '=' verbatim,
// with no BOM, blank-line, comment, or whitespace tolerance (a product writer
// emits none of those). EVERY canonical key is required: the program writes
// all of them, so a file short of any one is hand-damaged and refuses.
//
// This schema owns the zoom-level range too: a zoom outside
// kFitFileLevel..kMaxNumericLevel is refused here in both products. What
// stays caller-side, ON TOP of this schema, is the audio-relative past-EOF
// wall check on authored marker/trim positions, which needs the loaded
// source's frame count this reader never sees: both loaders (GUI file_loader
// and CLI) run it load-fatally through the one shared
// first_past_eof_wall_defect (marker_store_validate.h). Persisted
// viewport/playhead positions are NOT range-checked at load — they are
// display scratch, not authored data, and the runtime clamps own any
// out-of-range value. This schema does not check trim bound ordering itself: GUI load
// reads the raw values, then the caller's auto-clear pass clears a crossed
// or equal per-tab pair (one stderr line) before the store can rest; the
// CLI loads the values verbatim, and a still-crossed or -equal pair simply
// makes its render plan fall back to rendering untrimmed.

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
// false means the on-disk value was the literal `-1` (the unset spelling); a
// blank or absent key is load-fatal. The paired _frame field must not be read
// when has_* is false.
struct SettingsTrim {
    bool    has_begin   = false;
    int64_t begin_frame = 0;
    bool    has_end     = false;
    int64_t end_frame   = 0;
};

// One tab's view-state band: viewport / zoom / playhead scratch, the
// read-only flag, and the trim pair.
struct SettingsFileTab {
    int64_t viewport_start = 0;
    int     zoom           = 0;
    int64_t playhead       = 0;
    bool    read_only      = false;
    SettingsTrim trim;
};

struct SettingsFile {
    // The typed engine block. Every engine key is required in the file, so all
    // six fields are reader-assigned (the provenance values may be blank).
    EngineSettings engine;

    SettingsFileTab tab_a;
    SettingsFileTab tab_b;

    // Every canonical key is required, so the reader always assigns these
    // fields; the member initializers below are construction-state only.
    bool   follow                  = true;
    char   active_audio_view       = 'S';   // S | T
    char   active_markers_view     = 'W';   // W | P
    char   active_tab_view         = 'A';   // A | B
    float  playback_speed          = 0.7f;  // preset vocabulary only
    double font_size               = 11.0;  // points, [6, 72]
    // GUI-kind launcher for the `l` render-listen command: an external player
    // name or path. A BLANK value (`audio_player=`) is the deliberate
    // no-player opt-out — the only spelling of it. The key is required, so the
    // reader always assigns this field.
    std::string audio_player;
};

// Parse and validate the whole `.settings` file at `path`. An unopenable
// file is an error (the GUI writes the template before its first read; the
// CLI checks existence with its own message first). Errors carry a
// "line N: " prefix where a line is at fault; callers add the path context.
std::expected<SettingsFile, std::string> read_settings_file(
    const std::string& path);

// Strict line scanner for the settings format. The whole-file reader —
// read_settings_file (`.settings`) — builds on this: it owns the lexical
// contract, while the reader keeps the per-key vocabulary and validation
// arms.
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

// Scan `in` under the shared lexical contract — split each line at its first
// '=' verbatim (no BOM, blank-line, comment, or whitespace tolerance),
// empty-key and keyless-line refusal, duplicate-key refusal against a seen
// set — and invoke `on_pair` once per line. After the loop, enforce that
// every canonical key is present (the required-key list in settings_file.cpp),
// reporting the first missing one. Returns the first error (lexical, callback,
// or missing-required-key), or success.
std::expected<void, std::string> scan_settings_file(std::istream& in,
                                                    const SettingsLineFn& on_pair);

// Shared engine-key arm. If `key` is a canonical engine key, validate
// (key, value) into `engine` and return the outcome (`{}` on accept, a
// bad_value refusal otherwise). If `key` is not an engine key, returns
// std::nullopt so the caller falls through to its own per-key arms.
std::optional<std::expected<void, std::string>> try_engine_key(
    int ln, const std::string& key, const std::string& value,
    EngineSettings& engine);

// The typed result of a GUI-kind (key, value) grammar check. The consumers
// (the whole-file reader and the settings editor) route by key name / tab
// suffix, so only the parsed-value members below are read; each member's
// comment names the key(s) that fill it.
struct GuiSettingValue {
    bool        b    = false;   // follow, tab_X_read_only
    char        c    = 0;       // active_audio_view / _markers_view / _tab_view (S/T, W/P, A/B)
    int         i    = 0;       // tab_X_zoom
    int64_t     i64  = 0;       // tab_X_viewport_start / _playhead_cursor / _trim_*
    float       f    = 0.0f;    // playback_speed
    double      d    = 0.0;     // font_size
    bool trim_unset  = false;   // tab_X_trim_*: the value was -1 (bound unset)
    std::string text;           // audio_player
};

// The single grammar/vocabulary owner for GUI-kind settings values — the
// GUI-kind sibling of validate_engine_setting. STATE-FREE: it parses and
// vocabulary-checks one (key, value) pair; state-dependent rules stay at the
// boundaries (the load-side trim past-EOF walls live in
// first_past_eof_wall_defect; the editor adds its read-only-tab trim refusal,
// its own trim walls, and active/inactive routing on top). Both the whole-file
// reader below and the GUI settings editor call this, so a spelling is loadable
// iff it commits. Returns std::nullopt when `key` is not a GUI-kind key (the
// caller falls through to its own unknown-key handling, mirroring
// try_engine_key); an expected error carries the bad_value-style reason text.
std::optional<std::expected<GuiSettingValue, std::string>> validate_gui_setting(
    const std::string& key, const std::string& value);

}  // namespace warptempo_settings
