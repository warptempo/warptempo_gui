#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

// THE DEVICE CONFIG — the preferences that describe the MACHINE rather than the
// piece (architect 2026-08-27). Two keys live here and nowhere else:
//
//   gui_scale=<percent>      the GUI's one scale axis, an integer [50, 400]
//   audio_player=<name>      the `l` command's external player; BLANK = none
//
// WHY IT EXISTS. Both were `.settings` keys until 2026-08-27, which made them
// facts about the PIECE: the same project opened on the laptop and on the
// tablet wants 100 and 250, and the laptop can spawn audacious where the tablet
// has nothing spawnable at all. Carrying them in the sidecar meant every sync
// of a project between the two devices had to rewrite them on the way over and
// put them back on the way home. They are the panel's business, so they follow
// the panel. The sidecar schema keeps everything that is about the music
// (settings_file.h, where the retired-key record for both lives).
//
// WHERE IT LIVES: `$XDG_CONFIG_HOME/warptempo_gui/config`, falling back to
// `$HOME/.config/warptempo_gui/config` — the same resolution shape the render
// cache uses for XDG_CACHE_HOME (render_cache.cpp), and ONE resolver with no
// `#ifdef` in it: the Android backend points XDG_CONFIG_HOME at the app's
// private internal directory before anything reads it (android_main,
// platform_android.cpp), exactly as it already does for the cache home.
//
// THE STRICTNESS POSTURE IS THE SIDECAR'S, DELIBERATELY. The file is
// program-written — the first run stamps it from the backend's own template and
// every later commit rewrites it — so any violation is a hand edit, which the
// two-category rule makes ADVERSARIAL: whole-file schema, EXACTLY the two keys
// and each of them exactly once, every key REQUIRED, one canonical spelling per
// value, and the FIRST error is fatal at startup with a blunt terminal line
// naming the path and the offending line. No repair, no partial apply, no
// silent fallback to defaults — a fallback would silently discard a value the
// user typed. The lexing itself is not respelled here: the shared scanner
// (warptempo_settings::scan_key_value_file, settings_file.h) owns "split at the
// first '=', no blank/comment/whitespace tolerance, no duplicate key, every
// required key present", and this file owns only the per-key grammar over it.
//
// ORDER IS THE WRITER'S, NOT THE READER'S — the sidecar's own posture again.
// The key list in device_config.cpp is the EMITTED order (gui_scale, then
// audio_player) and it is what every file this program writes carries; the
// shared scanner checks MEMBERSHIP, duplicates and presence and never position,
// so a hand-swapped pair still loads. That costs nothing and buys the sidecar's
// symmetry: there, kSettingsOrder owns the write order and the reader has
// always been order-insensitive. A position rule would be a second guard for a
// state the writer cannot produce.
//
// NO UNDO, NO DIRTY, NO Ctrl+S. Both keys are history-less GUI-kind values
// committed through their own gesture chokepoints, and each of those
// chokepoints writes this file immediately (GuiInputHandler::apply_gui_scale
// for the scale, the settings editor's `audio_player=` arm for the player), so
// the file is always the last committed state and a save carries neither.

// The whole file, typed. The member defaults are CONSTRUCTION STATE, not load
// fallbacks: both keys are required, so a successful read always assigns both.
struct DeviceConfig {
    int         gui_scale = 100;
    std::string audio_player;
};

// THE gui_scale RANGE — the ONE owner, moved here from the `.settings` schema
// 2026-08-27 with the key (architect approval 2026-08-27). Both askers call it
// and neither respells the bracket: this file's reader, and the settings
// editor's `gui_scale=` red-flash ("loadable iff it commits", the standing
// rule).
//
// 100 is the design baseline (1920x1080, the supported laptop resolution); 200
// is the 4K case; 400 is the fine-panel ceiling — a 280 dpi tablet panel, where
// matching a coarser display's apparent size lands above 200 (225 % gives the
// 1024 logical width of the retired rig, ~305 % matches an 82 PPI external);
// 50 is the half-size floor, which is where every structural dimension in
// render.h's scaled_px accessors still has a floor holding it above zero.
//
// THE LAYOUT IS NOT WIDENED WITH THE CEILING, deliberately: below roughly
// 925 px of LOGICAL width (device width divided by the factor) the icon row's
// twenty-six-button left-to-right walk runs past the window's right edge — which
// is why the tablet's own 250 shaves the rightmost history icon by ~3 authored
// px — and the redesign
// carries no collision rule anywhere — the crop-at-the-floor allowance recorded
// at kMinWindowWidthPx (render.h) is the standing answer. A scale is a
// VOCABULARY; which of its values lays out well is the architect's call on his
// own panel, not a validator's.
inline constexpr bool is_gui_scale_percent(int64_t v) {
    return v >= 50 && v <= 400;
}

// The canonical on-disk spelling of a scale percent: plain digits, exactly the
// `%d` output the reader's parse_authored_frame arm accepts back. THE ONE
// SERIALIZER for the value — this file's writer and the settings editor's Tab
// autocomplete recall (recall_gui_setting_value, settings_io.h) both call it,
// so recall and file can never diverge.
std::string format_gui_scale_percent(int percent);

// The resolved config path, or an EMPTY path when neither XDG_CONFIG_HOME nor
// HOME is set (the loader turns that into its own fatal line; the writer
// reports the failure and writes nothing).
std::filesystem::path device_config_path();

// The file's exact bytes for this value set — the string half write_device_config
// hands to the atomic writer, and the first-run template's bytes too, so the
// stamped file and a committed one are one format by construction.
std::string format_device_config_text(const DeviceConfig& cfg);

// Parse and validate the whole config file at `path` under the schema at the
// head of this file. Errors carry a "line N: " prefix where a line is at fault;
// the caller adds the path context.
std::expected<DeviceConfig, std::string> read_device_config(
    const std::filesystem::path& path);

// Write the live values atomically (temp + fsync + rename, through the shared
// atomic writer), creating the parent directory if it is missing. Returns false
// on any I/O failure, having printed one stderr line — a failed persist is
// advisory, never fatal: the live value stands for this session.
//
// THE TWO CALLERS ARE THE TWO COMMITS, and this is their inventory: the scale's
// chokepoint GuiInputHandler::apply_gui_scale (input_handler.cpp) and the
// settings editor's `audio_player=` arm (settings_editor.cpp). A same-value
// commit never reaches either — both gate the no-op ahead of the write — so a
// file rewrite means a value actually moved.
bool write_device_config(const DeviceConfig& cfg);

// STARTUP: the config, created from `first_run_template` if the file does not
// exist yet and then read back like any other. The template is the running
// BACKEND's answer (GuiPlatform::device_config_defaults — a platform fact, not
// a GUI one: the laptop wants 100 % and audacious, the tablet 250 % and no
// player at all), so a first run on either device lands a file that is already
// right for it and the user edits from there rather than from a wrong guess.
// A missing parent directory is created.
//
// The error is the whole failure vocabulary of this file: an unresolvable
// config home, a create that failed, an unopenable file, or the reader's first
// schema refusal. Every one of them is FATAL at the caller (gui_main) — the
// blunt terminal line and no window.
std::expected<DeviceConfig, std::string> load_device_config(
    const DeviceConfig& first_run_template);
