#pragma once

#include "engine_settings.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <istream>
#include <optional>
#include <span>
#include <string>

// Whole-file strict schema for the `.settings` sidecar — the single owner
// of key membership, duplicate detection, and per-key value grammar over an
// all-required canonical key set, run by BOTH products
// (GuiFileLoader::load_file and warptempo_cli) before any value is applied.
// A sidecar set is loadable in the GUI and the CLI, or in neither.
//
// IT IS THE PIECE'S FILE AND NOTHING ELSE (architect 2026-08-27, the grant this
// paragraph lands under, extended the same day by a fifth grant): the four
// keys that were about the DEVICE or about nothing left it that day —
// `gui_scale`, `audio_player` and `projects_repo` to the GUI's own per-device
// config (src/gui/device_config.h), `playback_speed` to retirement — and the
// record of all four, with the consequence for a file still carrying one, is
// at kCanonicalSettingsKeys in settings_file.cpp. `audio_player` DID NOT
// SURVIVE THE MOVE: it retired whole on 2026-08-28 when the GUI grew its own
// render player, and a config still
// carrying an `audio_player=` line is unknown-key fatal there exactly as a
// sidecar carrying one is here (architect approval 2026-08-28, comment-only).
// THE DEVICE CONFIG IS FIVE KEYS — `gui_scale`, `projects_repo`,
// `projects_path`, `last_project`, `sync_path`, in the writer's own order
// (it was four between `audio_player`'s retirement and 2026-08-30, when
// `sync_path` arrived to name where Synchronize to external storage mirrors,
// retiring the discovery that had found that destination). None of the five
// has ever been this schema's business (architect approval 2026-08-30,
// comment only).
//
// The file is program-written (Ctrl+S / the first-open template), so every
// violation is adversarial under the two-category rule and load-fatal with
// the FIRST error only: an unknown key, a keyless line, a duplicate of ANY
// key, a malformed or out-of-vocabulary value, and a missing canonical key all
// refuse. Lexing is byte-exact:
// each line is split at its first '=' verbatim, with no BOM, blank-line,
// comment, or whitespace tolerance (a product writer emits none of those,
// so a '#' line is not a comment — it takes the keyless-line refusal like any
// other). The program writes every key it knows, so a file short of a REQUIRED
// one is hand-damaged and refuses.
//
// NO KEY IS OPTIONAL — the required-key rule speaks for the whole canonical set
// without exception, and the one exception it ever had is retired: from
// 2026-08-03 `projects_repo` was recognized but not demanded, and on 2026-08-04
// the architect made it required like every other key (approval recorded at
// kCanonicalSettingsKeys, settings_file.cpp; the key itself left the schema
// for the device config 2026-08-27). Recognition (validate_gui_setting) and
// requirement (kCanonicalSettingsKeys) remain SEPARATE LISTS, so the schema can
// still express a known-but-not-demanded key; nothing in it is one today, and a
// new key belongs on both.
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
// header. BOTH PRODUCTS ASK IT, and that is the entire reason the predicate
// sits in a shared header at all — the two must never disagree about what
// "untrimmed" means.
// (architect approval 2026-07-30 — the same grant that retired the `-1`
// grammar above.)
//
// WHO ASKS, AND HOW — BY KIND, NOT BY MEMBERSHIP LIST (architect approval
// 2026-08-03 for this comment-only correction). What stood here named three
// kinds and silently under-counted the consumers, which is what a hand-kept list
// in a shared header does as the callers grow. THE INVARIANT that does hold, and
// the one worth stating: every consumer CALLS this predicate — no site spells
// the compare a second time — and inside the GUI most of them ask through the
// single TrimState forwarder (trim_is_full_window, app_state.h). The kinds:
//   - the RENDER ORCHESTRATORS, in both products: a full window builds NO trim
//     plan, so plan_trim and its vocabulary see proper SUB-WINDOWS only;
//   - the PLAYBACK / NAVIGATION range owners: a full source pair becomes the
//     whole live domain rather than a mapped inclusive end, in both views;
//   - the RENDER FINGERPRINT: a full window hashes as the old unset bytes, which
//     is what keeps pre-arc untrimmed renders reusable;
//   - the CROSSED-PAIR NORMALIZATIONS (the load reset, the shared trim commit
//     tail, the settings editor's inactive band), where THE ORDER IS THE RULE —
//     the recognition runs AHEAD of the crossed compare, so a one-frame source's
//     canonical [0, 0] is never classified as a crossed pair;
//   - the GESTURE IDENTITY questions, which ask only whether there is anything
//     to do: an already-full maximize and a nothing-to-frame span framing.
// A NEW CONSUMER JOINS A KIND — it does not need this comment edited.
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

// THE WAVEFORM MAGNIFICATION LEVEL — the persisted vocabulary of the
// `waveform_magnification_level` key, and the ONE owner of its range. It lives
// here for the reason kMinZoom/kMaxZoom do: the schema is run verbatim by both
// products, so an out-of-range value must refuse identically in warptempo_gui
// and warptempo_cli even though only the GUI has pixels to apply it to.
// (architect approval 2026-08-26 — the settings/parser grant this key landed
// under, extended the same day to the first retune of it, and again
// 2026-08-27 to the second.)
//
// THE VALUE IS A COUNT OF DOUBLINGS, not a factor: the ladder is ×2 per step,
// so the GAIN is 2^level — 1, 2, 4, 8, 16 — and level 0 is the untouched
// picture. THE GAIN IS THE GUI'S OWN DERIVED FACT and is spelled once there
// (waveform_magnification_gain, render.h); the schema owns the RANGE alone,
// which is all a value arm can check.
//
// CAPPED AT ×16 because nothing above it is ever wanted: ×8 already clips the
// quietest classical passages and one more step covers the quietest masters.
// A WHOLE DOUBLING PER STEP is the coarser ladder the architect settled on
// 2026-08-27: the √2 ladder of the day before had twice the rungs and they
// were not worth walking, so a press is a doubling again and four of them
// reach the cap.
//
// THE LEVEL SCALES THE WAVEFORM PICTURE AND NOTHING ELSE: it is not a gain on
// the audio — no sample, no playback path and no render reads it; the CLI
// parses it and ignores it like every other GUI-kind key. Range membership is
// asked through the predicate below and never re-spelled — the schema's value
// arm and the GUI's own applier both call it.
inline constexpr int kWaveformMagnificationLevelMax = 4;
inline constexpr bool is_waveform_magnification_level(int64_t v) {
    return v >= 0 && v <= kWaveformMagnificationLevelMax;
}

// (kDefaultProjectsRepo LEFT THIS HEADER 2026-08-27 with the `projects_repo`
// key, architect approval 2026-08-27: the repository is a fact about the one
// user's one device config now, and the constant lives beside that file's
// template stamp, src/gui/device_config.h. Nothing in the parser reads it.)

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
    // KEEP VIEWPORT CENTERED ON PLAYHEAD — the `y` lamp's persisted
    // preference, `follow`'s sibling in every mechanical respect: a required
    // GUI-kind boolean on the same parse_bool_token grammar, default false.
    // While the lamp is lit the GUI derives the viewport from the playhead;
    // the CLI parses the key and ignores it like every other GUI-kind key.
    // Checkpoints committed before the key leave the `h` walk through the
    // same strict gate every schema addition costs — NO migration and no
    // reader leniency, the magnification key's precedent exactly.
    // (architect approval 2026-08-31 — "the parser's non-engine-modifying
    // keys are ok to touch".)
    bool   centered                = false;
    char   active_audio_view       = 'S';   // S | T
    char   active_markers_view     = 'W';   // W | P
    char   active_tab_view         = 'A';   // A | B
    // (FIVE FIELDS LEFT THIS STRUCT WITH THEIR KEYS — the retired-key record
    // is at kCanonicalSettingsKeys, settings_file.cpp. `font_size` went with
    // row 7's monospace deletion, architect approval 2026-08-01; `gui_scale`,
    // `audio_player`, `projects_repo` and `playback_speed` went 2026-08-27,
    // architect approval 2026-08-27 — the first three to the GUI's per-device
    // config (src/gui/device_config.h, which owns their types, their grammars
    // and their semantics now), the fourth to retirement. `audio_player` then
    // RETIRED THERE TOO on 2026-08-28, with the in-app render player that
    // replaced the spawn (architect approval 2026-08-28, comment-only), which
    // left that config four keys until `sync_path` made it FIVE on 2026-08-30
    // — no field of this struct's ever having been that one (architect
    // approval 2026-08-30, comment only). Nothing in either
    // product sizes text from a setting, nothing in either product plays at a
    // speed other than the source's own, nothing in either product spawns a
    // player, and the repository that is the
    // projects home is the one user's one device's fact, not each piece's.)
    // THE WAVEFORM'S VISUAL MAGNIFICATION — the DOUBLING COUNT of the
    // ladder above, whose gain the GUI derives and applies at the tip mapping
    // of every waveform picture (the plate and the overview strip alike),
    // CLAMPED to the lane so a loud passage clips flat at the edges while its
    // troughs still dip. THE PICTURE ONLY: it touches no sample, no playback
    // path and no render, and the CLI reads it and ignores it exactly as it
    // ignores every other GUI-kind key here.
    // The key is required, so the reader always assigns this field; the
    // initializer is construction state.
    // (architect approval 2026-08-26 — the settings/parser grant this key
    // landed under, extended the same day to the first retune of it, and again
    // 2026-08-27 to the second.)
    int    waveform_magnification_level = 0; // [0, kWaveformMagnificationLevelMax]
};

// Parse and validate the whole `.settings` file at `path`. An unopenable
// file is an error (the GUI writes the template before its first read; the
// CLI checks existence with its own message first). Errors carry a
// "line N: " prefix where a line is at fault; callers add the path context.
//
// `path_free_reason` is the marker loaders' out-parameter, with their
// contract (warpmarkers_parse.h; architect approval 2026-09-02, the granted
// frozen touch): when given, THE ONE REFUSAL THAT NAMES THE PATH — the
// unopenable file — writes its words with no path in them there ("could not
// open"), while the returned string stays the composed sentence, so the CLI's
// line is byte-identical and a card's composer, which already names this
// file, can name it once. A line-numbered schema error leaves it untouched.
std::expected<SettingsFile, std::string> read_settings_file(
    const std::string& path,
    std::optional<std::string>* path_free_reason = nullptr);

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

// THE SAME SCAN OVER A CALLER-SUPPLIED REQUIRED-KEY SET — the shared lexical
// contract with a different SCHEMA over it, and the whole of what
// scan_settings_file is (that function is this one plus the `.settings`
// canonical list). It exists because the GUI's per-device config
// (src/gui/device_config.h) is the same kind of file — program-written,
// `key=value` lines, every key required, one canonical spelling per value —
// and a second hand-spelled copy of "split at the first '=', refuse a keyless
// line, refuse a duplicate, refuse a missing required key" would be exactly the
// duplicate predicate the validation topology forbids. The device config's own
// per-key grammar stays its own; only the lexing and the requirement check are
// shared. `required_keys` is borrowed for the call.
// (architect approval 2026-08-27 — the device config grant.)
std::expected<void, std::string> scan_key_value_file(
    std::istream& in, const SettingsLineFn& on_pair,
    std::span<const char* const> required_keys);

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
// comment names the key(s) that fill it. (THE `float f` MEMBER WENT WITH
// `playback_speed` 2026-08-27, architect approval 2026-08-27: it was that key's
// alone, and no key in the schema carries a float now. THE `std::string text`
// MEMBER WENT WITH `projects_repo` the same day under the same day's fifth
// grant: it carried that key and, until 2026-08-09, the four render-environment
// `*_hash` keys, and no free-text GUI-kind key is left in the schema — the
// free-text keys that remain are all engine keys, typed into EngineSettings.)
struct GuiSettingValue {
    bool        b    = false;   // follow, centered, tab_X_read_only
    char        c    = 0;       // active_audio_view / _markers_view / _tab_view (S/T, W/P, A/B)
    int64_t     i64  = 0;       // tab_X_viewport_start / _playhead_cursor / _trim_*, waveform_magnification_level
    double      d    = 0.0;     // tab_X_zoom
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
