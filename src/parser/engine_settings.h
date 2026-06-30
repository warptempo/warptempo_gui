#pragma once

#include <expected>
#include <optional>
#include <string>

// Typed view of the engine-relevant settings keys. Promoted onto
// AppState as the live authoring store; carried by RenderRequest as the
// only engine-settings carrier. Member-default-initialized to the values
// format_default_settings_template emits (modulo title, which defaults to
// empty and is overwritten at source load from the stem). N and the
// phase-reset lead-in are no longer fields here — both are locked to
// the locked geometry constants in engine/engine_geometry.h (kN = 4096;
// lead-in = two synthesis hops). bpm, notes, url, and cover are inert
// provenance: free text,
// unvalidated, never read by the engine or acted on by the GUI.
struct EngineSettings {
    std::string title;
    std::string output_format            = "wav";
    double      scale                    = 1.0;
    std::string bpm;   // BPM render descriptor, e.g.
                       // "36 beats @ 220 bpm from 00:32.008 to 00:46.562".
                       // Empty when no BPM render is the current baseline.
                       // Informational only — no engine or GUI effect.
    std::string notes; // Free-text provenance (working notes, one line).
                       // Unvalidated, empty by default.
    std::string url;   // Free-text provenance (source/target URL or any
                       // string). Unvalidated, empty by default.
    std::string cover; // Free-text provenance (cover-art path or URL).
                       // Unvalidated, empty by default.
    bool        limiter                  = true;
};

// Identifier for one field of EngineSettings. Stored on each
// EnginePassthrough entry of kSettingsOrder (settings_io.cpp) so the
// serializer can switch over the typed field at write time without a
// string-key lookup. Order is incidental — only the value is read.
enum class EngineField {
    Title,
    Scale,
    Bpm,
    Notes,
    Url,
    Cover,
    OutputFormat,
    Limiter,
};

// True iff `key` is one of the canonical engine setting keys
// (title, scale, bpm, notes, url, cover, output_format, limiter).
bool is_canonical_engine_key(const std::string& key);

// Validate (key, value) per the canonical engine rules and assign to the
// corresponding EngineSettings field on success. On failure, leaves `out`
// untouched and fills `reason` with a short human constraint string
// (e.g. "must be one of {wav, framemap, tempomap}"). Caller wraps with
// the surrounding "key 'X' has invalid value 'Y':" prefix. Used by both
// read_engine_settings_from_file and GuiSettingsEditor::commit.
//
// Returns false with reason "unknown engine key" if `key` is not in
// the canonical engine set — defensible against callers that didn't
// pre-gate on is_canonical_engine_key.
bool validate_engine_setting(const std::string& key,
                             const std::string& value,
                             EngineSettings& out,
                             std::string& reason);

// Current on-disk string form of engine `key`'s value in `es`, formatted
// exactly as the settings writer emits it (%.6f for scale, true/false for
// limiter, verbatim for the string fields). Returns std::nullopt if `key`
// is not a canonical engine key; the empty string is a valid result (an
// unset free-text field). Inverse of validate_engine_setting; used by the
// settings prompt's Tab autocomplete.
std::optional<std::string> format_engine_setting_value(
    const EngineSettings& es, const std::string& key);

// Strict deserializer. Walks `path` looking for canonical engine-key
// lines and returns the populated typed struct. Lines whose key is not a
// canonical engine key are ignored (view-state keys, unknown keys, blank
// and comment lines). Performs no I/O beyond reading `path`: it does not
// log. On the first violation it encounters (file not openable, duplicate
// key, invalid value, or — after the scan — a missing required key) it
// returns std::unexpected carrying that one reason; the caller decides how
// to surface it. Required keys are title, output_format, scale, limiter;
// bpm, notes, url, and cover are optional.
std::expected<EngineSettings, std::string> read_engine_settings_from_file(
    const std::string& path);
