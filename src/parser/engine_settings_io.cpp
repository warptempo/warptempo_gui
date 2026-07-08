#include "engine_settings.h"

#include "parse_text_util.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <expected>
#include <fstream>
#include <limits>
#include <set>
#include <string>

namespace {

using warptempo_parse::trim_ws;

// Strict value parsers for validate_engine_setting. Each consumes the
// entire string; trailing garbage, non-finite doubles, and out-of-range
// integers are rejected.

bool parse_double_strict(const std::string& s, double& out) {
    if (s.empty()) return false;
    try {
        std::size_t pos = 0;
        const double v = std::stod(s, &pos);
        if (pos != s.size()) return false;
        if (!std::isfinite(v)) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_bool_strict(const std::string& s, bool& out) {
    if (s == "true"  || s == "1" || s == "yes" || s == "on")  { out = true;  return true; }
    if (s == "false" || s == "0" || s == "no"  || s == "off") { out = false; return true; }
    return false;
}

// The canonical engine key set. Membership only — order is irrelevant.
constexpr const char* kEngineKeys[] = {
    "title", "scale", "bpm", "notes", "url", "cover", "output_format", "limiter",
};

} // namespace

bool is_canonical_engine_key(const std::string& key) {
    for (const char* k : kEngineKeys) {
        if (key == k) return true;
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
        double v;
        if (!parse_double_strict(value, v) || !(v > 0.0)) {
            reason = "must be a finite double strictly greater than 0";
            return false;
        }
        // The canonical writers persist scale as %.6f, so a value that does
        // not round-trip that serialization exactly either fails its own
        // reload (sub-half-micro values serialize as 0.000000, which the
        // greater-than-zero check above rejects) or silently changes value
        // across save and reload. The implied lower bound in the one-micro
        // class also keeps build_warp_frame_map's target-delta division finite: a
        // subnormal scale could otherwise drive delta_tgt to inf and emit
        // non-finite map artifacts as success.
        char buf[64];
        const int n = std::snprintf(buf, sizeof(buf), "%.6f", v);
        double rt;
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(buf) ||
            !parse_double_strict(buf, rt) || rt != v) {
            reason = "must survive the settings writer's %.6f serialization "
                     "unchanged (at most six decimal places of precision)";
            return false;
        }
        // Ruled authoring bound: scale must lie in [0.000001, 9.999999]
        // inclusive, preventing degenerate global tempo scales at authoring
        // rather than leaving them for the engine to catch. Because the %.6f
        // round-trip above has already passed, v is exactly representable at
        // six decimals, so these plain double comparisons against the two
        // six-decimal literals are exact at the boundaries — no epsilon or
        // string comparison is needed. This ceiling does not cap label-ref
        // implied multipliers; the engine's zero-cap dispatch refusal remains
        // the backstop for ref-implied extreme geometry.
        if (v < 0.000001 || v > 9.999999) {
            reason = "must be between 0.000001 and 9.999999";
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
        if (!parse_bool_strict(value, v)) {
            reason = "must be one of {true, false, 1, 0, yes, no, on, off}";
            return false;
        }
        out.limiter = v;
        return true;
    }
    reason = "unknown engine key";
    return false;
}

std::optional<std::string> format_engine_setting_value(
        const EngineSettings& es, const std::string& key) {
    if (key == "title")         return es.title;
    if (key == "output_format") return es.output_format;
    if (key == "scale") {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6f", es.scale);
        return std::string(buf);
    }
    if (key == "bpm")     return es.bpm;
    if (key == "notes")   return es.notes;
    if (key == "url")     return es.url;
    if (key == "cover")   return es.cover;
    if (key == "limiter") return std::string(es.limiter ? "true" : "false");
    return std::nullopt;
}

std::expected<EngineSettings, std::string> read_engine_settings_from_file(
        const std::string& path) {
    EngineSettings es{};
    std::set<std::string> seen;

    std::ifstream f(path);
    if (!f) {
        return std::unexpected("could not open '" + path + "'");
    }
    std::string line;
    bool first_line = true;
    while (std::getline(f, line)) {
        if (first_line) {
            warptempo_parse::strip_bom(line);
            first_line = false;
        }
        const std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = trim_ws(trimmed.substr(0, eq));
        const std::string value = trim_ws(trimmed.substr(eq + 1));
        if (key.empty()) continue;

        // Ignore anything that is not a canonical engine key:
        // view-state keys, legacy keys, typos, foreign keys.
        if (!is_canonical_engine_key(key)) continue;

        if (!seen.insert(key).second) {
            return std::unexpected("duplicate key '" + key + "'");
        }
        std::string reason;
        if (!validate_engine_setting(key, value, es, reason)) {
            return std::unexpected("key '" + key + "' has invalid value '" +
                                   value + "': " + reason);
        }
    }

    for (const char* k : {"title", "output_format", "scale", "limiter"}) {
        if (seen.count(k) == 0)
            return std::unexpected(std::string("missing required key '") + k + "'");
    }
    return es;
}
