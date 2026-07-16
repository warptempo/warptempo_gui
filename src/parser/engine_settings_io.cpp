#include "engine_settings.h"

#include "value_format.h"

#include <string>

namespace {

// The canonical engine key set, each paired with the typed EngineField it
// maps to. Order is irrelevant — only membership and the key<->field
// association matter. Shared by is_canonical_engine_key (membership) and
// format_engine_setting_value (key-to-field lookup ahead of delegating to
// format_engine_field_value).
struct EngineKeyEntry {
    const char* key;
    EngineField field;
};

constexpr EngineKeyEntry kEngineKeys[] = {
    { "title",         EngineField::Title },
    { "scale",         EngineField::Scale },
    { "bpm",           EngineField::Bpm },
    { "notes",         EngineField::Notes },
    { "url",           EngineField::Url },
    { "cover",         EngineField::Cover },
};

} // namespace

bool is_canonical_engine_key(const std::string& key) {
    for (const auto& entry : kEngineKeys) {
        if (key == entry.key) return true;
    }
    return false;
}

std::string default_render_title(const std::string& source_stem) {
    return source_stem + "-rendered";
}

bool validate_engine_setting(const std::string& key,
                             const std::string& value,
                             EngineSettings& out,
                             std::string& reason) {
    if (key == "title") {
        // One canonical spelling, no input latitude. The value arrives already
        // trimmed of surrounding whitespace by both boundaries
        // (scan_settings_file and the editor commit), so whitespace ownership
        // is theirs; the title is taken verbatim after three refusals: an empty
        // title, a double-quote-wrapped spelling (the quotes would be part of
        // the name, never a valid title), and a '/' (it would compose the
        // render path into a subdirectory or onto another file).
        if (value.empty()) {
            reason = "must be non-empty";
            return false;
        }
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            reason = "must not be wrapped in quotes";
            return false;
        }
        if (value.find('/') != std::string::npos) {
            reason = "must not contain '/'";
            return false;
        }
        out.title = value;
        return true;
    }
    if (key == "scale") {
        // Full positive double, the same rule as a marker scale value
        // (parse_value_double strictness plus a > 0 refusal), bounded by
        // the scale bracket (value_format.h: [kScaleMin, kScaleMax]). The
        // writers persist the padded shortest round-trip form (min 4
        // decimals), so any accepted value reloads bit-identically — no
        // serialization round-trip gate is needed. This single validator
        // serves both boundaries: a malformed .settings aborts the load, an
        // editor commit red-flashes.
        double v;
        if (!parse_value_double(value, v) || !(v > 0.0) ||
            v < kScaleMin || v > kScaleMax) {
            reason = "must be a finite double within [" +
                     format_value_double(kScaleMin, 4) + ", " +
                     format_value_double(kScaleMax, 4) + "]";
            return false;
        }
        out.scale = v;
        return true;
    }
    if (key == "bpm") {
        // Free-text provenance descriptor on one line. Accepted verbatim
        // (caller already trims the line); empty is the canonical unset
        // form (`bpm=`).
        out.bpm = value;
        return true;
    }
    if (key == "notes") {
        // Free-text provenance, one line. Accepted verbatim; empty is unset.
        out.notes = value;
        return true;
    }
    if (key == "url") {
        // Free-text provenance. Accepted verbatim; empty is unset.
        out.url = value;
        return true;
    }
    if (key == "cover") {
        // Free-text provenance (cover-art path or URL). Accepted verbatim;
        // empty is unset.
        out.cover = value;
        return true;
    }
    reason = "unknown engine key";
    return false;
}

std::string format_engine_field_value(const EngineSettings& es,
                                      EngineField field) {
    std::string out;
    switch (field) {
        case EngineField::Title:
            out = es.title;
            break;
        case EngineField::Scale:
            // Scale-like value form: padded shortest round-trip, min 4
            // decimals — exactly the bytes the settings writer emits.
            out = format_value_double(es.scale, 4);
            break;
        case EngineField::Bpm:
            out = es.bpm;
            break;
        case EngineField::Notes:
            out = es.notes;
            break;
        case EngineField::Url:
            out = es.url;
            break;
        case EngineField::Cover:
            out = es.cover;
            break;
    }
    return out;
}

std::optional<std::string> format_engine_setting_value(
        const EngineSettings& es, const std::string& key) {
    for (const auto& entry : kEngineKeys) {
        if (key == entry.key) return format_engine_field_value(es, entry.field);
    }
    return std::nullopt;
}

