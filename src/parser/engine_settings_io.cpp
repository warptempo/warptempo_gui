#include "engine_settings.h"

#include "parse_text_util.h"
#include "value_format.h"

#include <cctype>
#include <cmath>
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
    { "output_format", EngineField::OutputFormat },
    { "limiter",       EngineField::Limiter },
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
        // Strip a matching leading/trailing double-quote pair, then validate
        // non-empty after whitespace trim, no embedded newline, and no slash.
        std::string v = value;
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
            v = v.substr(1, v.size() - 2);
        }
        std::size_t a = 0;
        while (a < v.size() &&
               std::isspace(static_cast<unsigned char>(v[a]))) ++a;
        std::size_t b = v.size();
        while (b > a &&
               std::isspace(static_cast<unsigned char>(v[b - 1]))) --b;
        v = v.substr(a, b - a);
        if (v.empty()) {
            reason = "must be non-empty after whitespace trim";
            return false;
        }
        if (v.find('\n') != std::string::npos) {
            reason = "must not contain an embedded newline";
            return false;
        }
        if (v.find('/') != std::string::npos) {
            reason = "must not contain '/'";
            return false;
        }
        out.title = std::move(v);
        return true;
    }
    if (key == "output_format") {
        // wav is the finished-audio render; the three map formats write
        // artifacts instead of audio: warptempo_maps is the warp frame map
        // plus phase reset frame map pair (together exactly the engine's
        // input), generic_map the warp frame map alone
        // for generic external stretch consumers, midi_map the midi tempo
        // map for DAW hosts. There is no reset-alone format because a phase
        // reset frame map is meaningful only against the exact warp frame
        // map it was derived beside, so the reset artifact ships only inside
        // the warptempo_maps pair, while the warp map alone serves generic
        // consumers.
        if (value != "wav" && value != "warptempo_maps" &&
            value != "generic_map" && value != "midi_map") {
            reason = "must be one of {wav, warptempo_maps, generic_map, midi_map}";
            return false;
        }
        out.output_format = value;
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
    if (key == "limiter") {
        bool v;
        if (!warptempo_parse::parse_bool_token(value, v)) {
            reason = "must be one of {true, false, 1, 0, yes, no, on, off}";
            return false;
        }
        out.limiter = v;
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
        case EngineField::OutputFormat:
            out = es.output_format;
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
        case EngineField::Limiter:
            out = es.limiter ? "true" : "false";
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

