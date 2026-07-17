#pragma once

#include <expected>
#include <optional>
#include <string>

// Typed view of the engine-relevant settings keys. Promoted onto
// AppState as the live authoring store; carried by RenderRequest as the
// only engine-settings carrier. Member-default-initialized to the values
// format_default_settings_template emits (modulo title, which defaults to
// empty and is overwritten at source load from the stem). N is not
// a field here — it is the geometry constant in
// engine/engine_geometry.h (kN = 4096). bpm, notes, url, and cover are
// provenance: free text,
// unvalidated, never read by the engine's DSP; like every engine-settings
// field they participate in the render fingerprint, so an edit makes the
// next render fresh.
struct EngineSettings {
    std::string title;
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
};

// True iff `key` is one of the canonical engine setting keys
// (title, scale, bpm, notes, url, cover).
bool is_canonical_engine_key(const std::string& key);

// The default render title for a source: the source stem plus "-rendered".
// Sole consumer is the GUI first-open template (format_default_settings_template
// in settings_io.cpp), which assigns it at source load. The CLI reads titles
// from the .settings file through the shared schema, so it never composes one.
std::string default_render_title(const std::string& source_stem);

// Validate (key, value) per the canonical engine rules and assign to the
// corresponding EngineSettings field on success. On failure, leaves `out`
// untouched and fills `reason` with a short human constraint string
// (e.g. "must be a finite double within [0.5000, 2.0000]").
// Caller wraps with
// the surrounding "key 'X' has invalid value 'Y':" prefix. Used by the
// whole-file schema reader (read_settings_file) and
// GuiSettingsEditor::commit.
//
// Returns false with reason "unknown engine key" if `key` is not in
// the canonical engine set — defensible against callers that didn't
// pre-gate on is_canonical_engine_key.
bool validate_engine_setting(const std::string& key,
                             const std::string& value,
                             EngineSettings& out,
                             std::string& reason);

// Current on-disk string form of `field`'s value in `es`, formatted exactly
// as the settings writer emits it (padded shortest round-trip form at min 4
// decimals for scale — value_format.h — verbatim for the string fields).
// This is the single byte definition for engine
// field serialization: format_engine_setting_value below delegates to it,
// and every settings writer (write_settings_file,
// format_default_settings_template) reaches it through kSettingsOrder's
// typed EngineField rather than re-encoding fields itself.
std::string format_engine_field_value(const EngineSettings& es,
                                      EngineField field);

// Current on-disk string form of engine `key`'s value in `es`. Looks up
// `key`'s EngineField and delegates to format_engine_field_value. Returns
// std::nullopt if `key` is not a canonical engine key; the empty string is
// a valid result (an unset free-text field). Inverse of
// validate_engine_setting; used by the settings prompt's Tab autocomplete.
std::optional<std::string> format_engine_setting_value(
    const EngineSettings& es, const std::string& key);
