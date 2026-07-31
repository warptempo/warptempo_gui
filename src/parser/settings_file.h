#pragma once

#include "engine_settings.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <istream>
#include <optional>
#include <string>

// Whole-file strict schema for the `.settings` sidecar — the single owner
// of key membership, duplicate detection, and per-key value grammar over an
// all-required canonical key set, run by BOTH products
// (GuiFileLoader::load_file and warptempo_cli) before any value is applied.
// A sidecar set is loadable in the GUI and the CLI, or in neither.
//
// The file is program-written (Ctrl+S / the first-open template), so every
// violation is adversarial under the two-category rule and load-fatal with
// the FIRST error only: an unknown key, a keyless line, a duplicate of ANY
// key, a malformed or out-of-vocabulary value (an off-preset playback_speed
// included), and a missing canonical key all refuse. Lexing is byte-exact:
// each line is split at its first '=' verbatim, with no BOM, blank-line,
// comment, or whitespace tolerance (a product writer emits none of those,
// so a '#' line is not a comment — it takes the keyless-line refusal like any
// other). There are no optional keys: the program writes EVERY canonical key,
// so a file short of any one is hand-damaged and refuses.
//
// This schema owns the zoom-level range too: a zoom outside
// [kMinZoom, kMaxZoom] is refused here in both products. What
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
// makes its render plan fall back to rendering untrimmed. A trim value is a
// canonical whole source frame and nothing else — the `-1` unset spelling was
// deleted when the trim window became always-set
// (architect approval 2026-07-30), so a sidecar still carrying it is
// adversarial and refuses here.

// The persisted zoom-level vocabulary, enforced by this schema in both
// products. The zoom LEVEL is a real-valued exponent that rests anywhere in
// the ONE continuous domain [kMinZoom, kMaxZoom] — there is no sentinel and no
// fit-file mode. Full zoom-out rests at whole-song-visible, the top of a
// per-file effective ceiling; that ceiling is a GUI-runtime clamp
// (effective_max_zoom_level) and is deliberately NOT schema-checked here:
// persisted zoom is validated against the theoretical vocabulary only, and the
// runtime reclamp owns the per-file ceiling — the same display-scratch
// convention the persisted viewport/playhead values follow. ms-per-pixel(level)
// = 0.625 * 2^(level-1) (GUI-side, in main.cpp's samples_per_pixel_at), so an
// integer level reproduces the historical 2x-per-step ladder exactly, and the
// exponent yields spp = total/width exactly at the fit-equivalent level. The
// constants live here rather than in the GUI so an out-of-vocabulary persisted
// zoom refuses identically in the GUI and the CLI.
constexpr double kMinZoom = 1.0;
// kMaxZoom is DERIVED from audio_io's structural source caps, not invented. The
// longest loadable source is bounded by the RIFF uint32 data-chunk size limit
// (~4 GiB of PCM); the binding case is 24-bit stereo (6 bytes/frame) at the
// 44100 Hz rate floor -> 4294967295 / 6 ~= 715.8 M frames ~= 16232 s. The
// narrowest supported window is 640 px (kMinWindowWidthPx; waveform effective
// width 640). kMaxZoom is the smallest whole level whose visible span covers
// that worst case at that width: 0.625 * 2^(17-1) ms/px * 640 px = 26214.4 s
// >= 16232 s, while level 16 gives 13107.2 s < 16232 s -- so 17 is minimal.
// Consequence: for EVERY loadable file the fit level is below kMaxZoom by
// construction, so full zoom-out always rests at whole-song-visible.
constexpr double kMaxZoom = 17.0;

// One tab's trim in the .settings schema. Positions are whole source frames
// (int64_t), decoded via parse_authored_frame (frame_format.h) — BOTH values
// always meaningful. The unset spelling (`-1`) is GONE
// (architect approval 2026-07-30 — the trim always-set arc): a trim window
// always holds a full ordered pair, so a `-1` is now just a malformed value and
// refuses like any other. A blank or absent key stays load-fatal.
struct SettingsTrim {
    int64_t begin_frame = 0;
    int64_t end_frame   = 0;
};

// THE FULL-WINDOW PREDICATE — the ONE owner, deliberately here rather than in
// the GUI for exactly the reason kMinZoom/kMaxZoom live here: the recognition
// must be IDENTICAL in warptempo_gui and warptempo_cli, which share no other
// header (the render orchestrators, the two playback/navigation range owners
// and the fingerprint all ask this one question).
// (architect approval 2026-07-30 — the same grant that retired the `-1`
// grammar above.)
//
// The FULL window [0, total-1] IS SEMANTICALLY THE OLD UNSET STATE: it renders
// untrimmed (no trim plan at all), plays to the natural end, hashes like unset,
// and Home/End reach the song edges. At total == 1 the canonical full pair is
// [0, 0] — a one-frame source is load-legal and `begin < end` is impossible
// there, so the compare below recognizes it without a special case.
inline bool trim_window_is_full(int64_t begin_frame, int64_t end_frame,
                                int64_t total_frames) {
    return total_frames > 0 && begin_frame == 0 &&
           end_frame == total_frames - 1;
}

// One tab's view-state band: viewport / zoom / playhead scratch, the
// read-only flag, and the trim pair.
struct SettingsFileTab {
    int64_t viewport_start = 0;
    double  zoom           = 0.0;
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
    // GUI rendering scale as an integer PERCENT in [100, 400]; 100 is the
    // design baseline (1920x1080), 200 the 4K case. DORMANT: no renderer reads
    // this yet — the value is loaded, held, and written back, and the row-by-row
    // GUI redesign adds its consumers one row at a time
    // (architect approval 2026-07-30 — the settings/parser grant this dormant
    // key landed under).
    int    gui_scale               = 100;   // percent, [100, 400]
    // GUI-kind launcher for the `l` render-listen command: an external player
    // name or path. A BLANK value (`audio_player=`) is the deliberate
    // no-player opt-out — the only spelling of it. The key is required, so the
    // reader always assigns this field.
    std::string audio_player;
    // Render-environment attestation (env_fingerprint.h): the per-library
    // stat-identity digests recorded at the last save, one 16-lowercase-hex-digit
    // value per render-relevant shared library. Required keys like every
    // other; the value grammar (exactly 16 lowercase hex digits) is enforced
    // by validate_gui_setting in both products. These are stored identity,
    // not recipe: the render fingerprint never reads them.
    std::string libm_hash;
    std::string libmvec_hash;
    std::string fftw3_hash;
    std::string fftw3_threads_hash;
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
    int64_t     i64  = 0;       // tab_X_viewport_start / _playhead_cursor / _trim_*, gui_scale
    float       f    = 0.0f;    // playback_speed
    double      d    = 0.0;     // font_size, tab_X_zoom
    std::string text;           // audio_player, the four *_hash keys
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
