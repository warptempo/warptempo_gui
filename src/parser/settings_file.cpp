#include "settings_file.h"

#include "frame_format.h"
#include "parse_text_util.h"
#include "playback_speed_presets.h"
#include "value_format.h"

#include <cstdio>
#include <expected>
#include <fstream>
#include <set>
#include <string>

namespace {

using warptempo_parse::parse_bool_token;
using warptempo_parse::parse_double_strict;
using warptempo_parse::prefix_line_error;

// Every canonical .settings key, in kSettingsOrder's on-disk order. This is
// the membership SoT for the required-key check: EVERY key is required, so a
// file missing any one is load-fatal. It is the parser-side twin of the GUI
// writer's kSettingsOrder (settings_io.cpp) — adding a settings key touches
// BOTH lists (and, for its grammar, kEngineKeys/validate_engine_setting for an
// engine key or validate_gui_setting for a GUI-kind key). kEngineKeys stays
// file-local to engine_settings_io.cpp; this flat list owns membership
// completeness here.
constexpr const char* kCanonicalSettingsKeys[] = {
    "title", "scale", "bpm", "notes", "url", "cover",
    "active_audio_view", "active_markers_view", "active_tab_view",
    "playback_speed", "follow", "font_size", "audio_player",
    "tab_a_trim_begin", "tab_a_trim_end", "tab_a_read_only",
    "tab_a_viewport_start", "tab_a_zoom", "tab_a_playhead_cursor",
    "tab_b_trim_begin", "tab_b_trim_end", "tab_b_read_only",
    "tab_b_viewport_start", "tab_b_zoom", "tab_b_playhead_cursor",
    "libm_hash", "libmvec_hash", "fftw3_hash", "fftw3_threads_hash",
};

}  // namespace

namespace warptempo_settings {

std::unexpected<std::string> bad_value(int ln, const std::string& key,
                                       const std::string& value,
                                       const std::string& rule) {
    return prefix_line_error(
        ln, "key '" + key + "' has invalid value '" + value + "': " + rule);
}

std::expected<void, std::string> scan_settings_file(
        std::istream& in, const SettingsLineFn& on_pair) {
    std::set<std::string> seen;
    std::string line;
    int ln = 0;
    while (std::getline(in, line)) {
        ++ln;
        // Byte-exact lexing: split each line at its first '=', verbatim. No
        // BOM, blank-line, comment, or whitespace tolerance — a product writer
        // emits none of those, so a line without a '=' (an empty or comment
        // line included) is the keyless-line refusal, and a padded key or
        // value reaches the per-key grammar unmodified (vocabulary keys refuse
        // the padding; free-text fields take the bytes verbatim).
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            return prefix_line_error(ln, "not a key=value line");
        }
        const std::string key   = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key.empty()) {
            return prefix_line_error(ln, "empty key");
        }
        if (!seen.insert(key).second) {
            return prefix_line_error(ln, "duplicate key '" + key + "'");
        }
        auto r = on_pair(ln, key, value);
        if (!r) return std::unexpected(std::move(r.error()));
    }

    // The getline loop ends on eofbit (normal end of file) or on badbit (a
    // stream read failure mid-file). eofbit+failbit is the ordinary end of a
    // healthy file and parses on; badbit alone is a filesystem or media read
    // error, checked here BEFORE the required-key tail so a read that failed
    // after every canonical key already arrived can never be laundered into
    // a complete-looking file.
    if (in.bad()) {
        return std::unexpected(std::string("i/o read error"));
    }

    for (const char* k : kCanonicalSettingsKeys) {
        if (seen.count(k) == 0) {
            return std::unexpected(
                std::string("missing required key '") + k + "'");
        }
    }
    return {};
}

std::optional<std::expected<void, std::string>> try_engine_key(
        int ln, const std::string& key, const std::string& value,
        EngineSettings& engine) {
    if (!is_canonical_engine_key(key)) return std::nullopt;
    // Engine keys route through the single per-(key, value) validator
    // shared with the editor-commit boundary.
    std::string reason;
    if (!validate_engine_setting(key, value, engine, reason)) {
        return std::expected<void, std::string>(bad_value(ln, key, value, reason));
    }
    return std::expected<void, std::string>{};
}

std::optional<std::expected<GuiSettingValue, std::string>> validate_gui_setting(
        const std::string& key, const std::string& value) {
    using R = std::expected<GuiSettingValue, std::string>;
    auto err = [](std::string reason) -> R {
        return std::unexpected(std::move(reason));
    };

    GuiSettingValue out;

    // A per-tab key prefix selects the tab (routing is the caller's job); the
    // suffix selects the field. The two tabs share one grammar by construction.
    if (key.rfind("tab_a_", 0) == 0 || key.rfind("tab_b_", 0) == 0) {
        const std::string suffix = key.substr(6);
        if (suffix == "viewport_start" || suffix == "playhead_cursor") {
            // Canonical integer spelling via parse_authored_frame (digits only,
            // no sign/point/leading zeros, int64 overflow refused) — exactly
            // the writer's %lld output for these non-negative scratch values.
            int64_t v = 0;
            if (!parse_authored_frame(value, v))
                return err("must be a non-negative integer");
            out.i64 = v;
            return R(out);
        }
        if (suffix == "zoom") {
            // The zoom level is a real-valued exponent resting anywhere in the
            // one continuous vocabulary [kMinZoom, kMaxZoom]. One canonical
            // spelling: the shortest round-trip double text the writer emits
            // (format_value_double(v, 0), the min-0 gate the session-only
            // bpm-bracket bounds also use). An integer rest ("1".."17") is
            // exactly its own min-0 shortest form; a fractional rest writes e.g.
            // "3.7" (or the full shortest-round-trip digits) and reloads
            // bit-exactly. The per-file effective ceiling is a GUI-runtime
            // clamp, not checked here.
            double v = 0.0;
            if (!parse_value_double(value, v) ||
                format_value_double(v, 0) != value ||
                !(v >= kMinZoom && v <= kMaxZoom))
                return err("must be a zoom level");
            out.d = v;
            return R(out);
        }
        if (suffix == "read_only") {
            bool v = false;
            if (!parse_bool_token(value, v))
                return err("must be true or false");
            out.b = v;
            return R(out);
        }
        if (suffix == "trim_begin" || suffix == "trim_end") {
            // A trim value is a canonical whole source frame, nothing else: the
            // `-1` unset spelling died with the unset STATE
            // (architect approval 2026-07-30 — the trim window is always a full
            // ordered pair now),
            // so `-1` refuses through parse_authored_frame like any other
            // malformed value. The GUI can never write it, which makes a
            // sidecar still carrying one adversarial by the two-category rule:
            // load-fatal, first error only, identically in both products. The
            // past-EOF wall stays state-dependent (caller-side).
            int64_t v = 0;
            if (!parse_authored_frame(value, v))
                return err("must be a whole source-frame position");
            out.i64 = v;
            return R(out);
        }
        return std::nullopt;  // unrecognized tab suffix: not a GUI-kind key
    }

    if (key == "follow") {
        // Bools share parse_bool_token schema-wide (same grammar as the
        // per-tab read_only keys).
        bool v = false;
        if (!parse_bool_token(value, v))
            return err("must be true or false");
        out.b = v;
        return R(out);
    }
    if (key == "active_audio_view") {
        if (value != "S" && value != "T") return err("must be S or T");
        out.c = value[0];
        return R(out);
    }
    if (key == "active_markers_view") {
        if (value != "W" && value != "P") return err("must be W or P");
        out.c = value[0];
        return R(out);
    }
    if (key == "active_tab_view") {
        if (value != "A" && value != "B") return err("must be A or B");
        out.c = value[0];
        return R(out);
    }
    if (key == "playback_speed") {
        // Preset-vocabulary-only on disk, matched by exact TEXT: the GUI
        // authors playback_speed through the settings editor
        // (:playback_speed=), whose commit red-flashes any value outside
        // kPlaybackSpeedPresets (the shared source of truth), so any
        // off-preset spelling is a state the GUI can never produce. The table
        // pairs each on-disk spelling the writer emits (%.1f) with its
        // nearest-float value; a byte match adopts the paired float. No float
        // parse at the boundary — ".7", "0.70", and "00.7" are not the
        // spelling and refuse.
        for (const PlaybackSpeedPreset& p : kPlaybackSpeedPresets) {
            if (value == p.text) {
                out.f = p.value;
                return R(out);
            }
        }
        return err("must be a preset speed");
    }
    if (key == "font_size") {
        // One canonical spelling: the value must parse into [6, 72] AND
        // re-serialize byte-identically through the writer's %g form (the same
        // format format_nonengine_value uses), so "11" and "10.5" load while
        // "11.0" and "011" refuse.
        double v = 0.0;
        if (!parse_double_strict(value, v) || v < 6.0 || v > 72.0)
            return err("must be a number in [6, 72] in canonical spelling");
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", v);
        if (value != buf)
            return err("must be a number in [6, 72] in canonical spelling");
        out.d = v;
        return R(out);
    }
    if (key == "libm_hash" || key == "libmvec_hash" ||
        key == "fftw3_hash" || key == "fftw3_threads_hash") {
        // Render-environment attestation values: exactly 16 lowercase hex
        // digits, canonical spelling only — the exact bytes the writer emits
        // (compute_render_env_hashes renders every digest, the absent-library
        // sentinel included, in this one form). All four keys share the one
        // grammar.
        if (value.size() != 16) {
            return err("must be exactly 16 lowercase hex digits");
        }
        for (char c : value) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return err("must be exactly 16 lowercase hex digits");
            }
        }
        out.text = value;
        return R(out);
    }
    if (key == "audio_player") {
        // GUI-kind launcher for the `l` render-listen command: an external
        // player binary name or path. Any value is accepted (no path/binary
        // grammar — it is user-supplied), INCLUDING empty: an empty value is
        // legal and means "no external player". The key is always present in a
        // product-written .settings, so the shared schema loads `audio_player=`
        // in both products.
        out.text = value;
        return R(out);
    }

    return std::nullopt;  // not a GUI-kind key
}

}  // namespace warptempo_settings

std::expected<SettingsFile, std::string> read_settings_file(
        const std::string& path) {
    SettingsFile out;

    std::ifstream f(path);
    if (!f) {
        return std::unexpected("could not open '" + path + "'");
    }

    auto scan = warptempo_settings::scan_settings_file(
        f, [&out](int ln, const std::string& key,
                  const std::string& value)
                  -> std::expected<void, std::string> {
        using warptempo_settings::bad_value;

        if (auto e = warptempo_settings::try_engine_key(ln, key, value,
                                                        out.engine)) {
            return *e;
        }

        // The single GUI-kind grammar owner. std::nullopt means the key is
        // neither an engine key (checked above) nor a GUI-kind key — the
        // unknown-key case; an expected error carries the reason bad_value
        // composes. On success, store the typed value into the SettingsFile
        // fields; tab routing stays here (state the schema function is
        // deliberately blind to).
        auto g = warptempo_settings::validate_gui_setting(key, value);
        if (!g) {
            return prefix_line_error(ln, "unknown key '" + key + "'");
        }
        if (!*g) {
            return bad_value(ln, key, value, (*g).error());
        }
        const warptempo_settings::GuiSettingValue& gv = **g;

        SettingsFileTab* tab = nullptr;
        std::string suffix;
        if (key.rfind("tab_a_", 0) == 0) {
            tab = &out.tab_a;
            suffix = key.substr(6);
        } else if (key.rfind("tab_b_", 0) == 0) {
            tab = &out.tab_b;
            suffix = key.substr(6);
        }

        if (tab != nullptr) {
            if (suffix == "viewport_start") {
                tab->viewport_start = gv.i64;
            } else if (suffix == "zoom") {
                tab->zoom = gv.d;
            } else if (suffix == "playhead_cursor") {
                tab->playhead = gv.i64;
            } else if (suffix == "read_only") {
                tab->read_only = gv.b;
            } else if (suffix == "trim_begin") {
                // Both bounds are always meaningful
                // (architect approval 2026-07-30): the value grammar admits
                // only a canonical whole source frame, so it applies verbatim.
                tab->trim.begin_frame = gv.i64;
            } else if (suffix == "trim_end") {
                tab->trim.end_frame = gv.i64;
            }
        } else if (key == "follow") {
            out.follow = gv.b;
        } else if (key == "active_audio_view") {
            out.active_audio_view = gv.c;
        } else if (key == "active_markers_view") {
            out.active_markers_view = gv.c;
        } else if (key == "active_tab_view") {
            out.active_tab_view = gv.c;
        } else if (key == "playback_speed") {
            out.playback_speed = gv.f;
        } else if (key == "font_size") {
            out.font_size = gv.d;
        } else if (key == "audio_player") {
            out.audio_player = gv.text;
        } else if (key == "libm_hash") {
            out.libm_hash = gv.text;
        } else if (key == "libmvec_hash") {
            out.libmvec_hash = gv.text;
        } else if (key == "fftw3_hash") {
            out.fftw3_hash = gv.text;
        } else if (key == "fftw3_threads_hash") {
            out.fftw3_threads_hash = gv.text;
        }
        return {};
    });
    if (!scan) return std::unexpected(std::move(scan.error()));
    return out;
}
