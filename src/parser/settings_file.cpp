// Dead include removed under grant (architect approval 2026-08-02).
#include "settings_file.h"

#include "frame_format.h"
#include "parse_text_util.h"
#include "value_format.h"

#include <expected>
#include <fstream>
#include <set>
#include <span>
#include <string>

namespace {

using warptempo_parse::parse_bool_token;
// (A THIRD using-declaration used to sit here, for parse_text_util's strict
// whole-token double parser; it went with the font_size arm, its one consumer
// in this file — architect approval 2026-08-01 — and the helper itself was
// then deleted from parse_text_util.h outright, so there is nothing left to
// name (architect approval 2026-08-02, comment retold; no code changed here).
// This file's one double-valued key, zoom, parses through value_format.h's
// parse_value_double against its own canonical-spelling round-trip.)
using warptempo_parse::prefix_line_error;

// Every REQUIRED .settings key, in kSettingsOrder's on-disk order. This is the
// membership SoT for the required-key check: a file missing any key on this
// list is load-fatal. It is the parser-side twin of the GUI writer's
// kSettingsOrder (settings_io.cpp) — adding a settings key touches BOTH lists
// (and, for its grammar, kEngineKeys/validate_engine_setting for an engine key
// or validate_gui_setting for a GUI-kind key). kEngineKeys stays file-local to
// engine_settings_io.cpp; this flat list owns requirement here.
//
// EVERY KEY IS REQUIRED, NO EXCEPTIONS — the rule has been whole since
// 2026-08-04, when `projects_repo` joined this list at its kSettingsOrder
// position (architect approval 2026-08-04). It had been the schema's ONE
// optional key for one day (architect approval 2026-08-03), registered in
// validate_gui_setting below but deliberately absent here so that sidecars
// written before it existed kept loading; the architect retired that exception
// with his eyes open, and "recognized" and "required" are the same membership
// question for every key once more. (The key itself LEFT the schema
// 2026-08-27 — the record is below with the other departures — but the rule
// it briefly excepted stands for every key that remains.)
//
// THE FOUR RENDER-ENVIRONMENT ATTESTATION KEYS — `libm_hash`, `libmvec_hash`,
// `fftw3_hash`, `fftw3_threads_hash` — LEFT THE SCHEMA 2026-08-09 (architect
// approval 2026-08-09), with the load-time advisory notice they fed. The same
// consequence applies to them as to `font_size` before them: a `.settings`
// still carrying one of the four is load-fatal in both products by the
// ordinary UNKNOWN-key refusal below, hand-edit is the whole recovery, and
// there is no migration tool and no reader leniency.
//
// `waveform_magnification_level` JOINED 2026-08-26 (architect approval
// 2026-08-26), REQUIRED from its first day like every key since
// `projects_repo`'s one-day exception was retired — and it carries the standing
// consequence in the other direction: a `.settings` carrying no
// `waveform_magnification_level=` line is load-fatal in both products by the
// missing-required-key refusal below. Legacy on-disk formats are never
// supported here; the architect re-saves his projects (and the checkpoints
// committed before the key drop out of the `h` walk by the same gate, the
// font_size and attestation precedents verbatim). THE KEY WAS RENAMED LATER
// THE SAME DAY, when the ladder became a count of half-doublings rather than a
// factor: the first spelling `waveform_magnification` is now an UNKNOWN key and
// takes the unknown-key refusal above, which is the same re-save either way.
// THE LADDER WAS RETUNED AGAIN 2026-08-27 (architect approval 2026-08-27),
// from half-doublings to WHOLE DOUBLINGS over the shorter bracket [0, 4]: the
// name is unchanged, so a file carrying a level above 4 keeps a canonical key
// and takes the value refusal below instead — the same re-save once more.
//
// FOUR KEYS LEFT THE SCHEMA 2026-08-27 (architect approval 2026-08-27, twice
// that day — the device-config cut, then the fifth grant on these two files for
// `projects_repo`) and the consequence is the standing one, in the UNKNOWN-key
// direction: `playback_speed` RETIRED WHOLE — the architect runs 1.0
// everywhere, so the key, its preset vocabulary and the variable-speed
// machinery behind it are gone from both products; `gui_scale`, `audio_player`
// and `projects_repo` are per DEVICE rather than per piece and moved to the
// GUI's own device config (src/gui/device_config.h,
// `$XDG_CONFIG_HOME/warptempo_gui/config`), which the CLI has no business
// reading — the first two because the panel and the spawnable player are the
// machine's, the third because ONE user has ONE repository and the recheck
// reads it from the device now. ONE OF THE THREE DID NOT SURVIVE THE MOVE:
// `audio_player` RETIRED WHOLE from the device config on 2026-08-28, the GUI
// having grown a render player of its own, and a
// config still carrying an `audio_player=` line refuses there exactly as a
// sidecar carrying one refuses here (architect approval 2026-08-28,
// comment-only). THAT CONFIG IS NOW FIVE KEYS — `gui_scale`, `projects_repo`,
// `projects_path`, `last_project`, `sync_path`, the writer's own order (four
// between that retirement and 2026-08-30, when `sync_path` arrived with the
// mirror's configured destination) — and none of them is readable from here
// (architect approval 2026-08-30, comment only). A `.settings` still carrying
// any of the four
// is load-fatal in both products by the ordinary unknown-key refusal below —
// hand-editing the lines out is the whole recovery, NO migration and no reader
// leniency, exactly as for `font_size` and the four attestation keys before
// them; the architect hand-edits his own files. He accepted both consequences
// with his eyes open: a `renders/` recipe written before the cut refuses `'`,
// and every checkpoint committed before it drops out of the `h` walk through
// the same strict gate.
constexpr const char* kCanonicalSettingsKeys[] = {
    "title", "scale", "bpm", "notes", "url", "cover",
    "active_audio_view", "active_markers_view", "active_tab_view",
    "follow", "waveform_magnification_level",
    "tab_a_trim_begin", "tab_a_trim_end", "tab_a_read_only",
    "tab_a_viewport_start", "tab_a_zoom", "tab_a_playhead_cursor",
    "tab_b_trim_begin", "tab_b_trim_end", "tab_b_read_only",
    "tab_b_viewport_start", "tab_b_zoom", "tab_b_playhead_cursor",
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
    return scan_key_value_file(in, on_pair, kCanonicalSettingsKeys);
}

std::expected<void, std::string> scan_key_value_file(
        std::istream& in, const SettingsLineFn& on_pair,
        std::span<const char* const> required_keys) {
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

    for (const char* k : required_keys) {
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
    // THE KEYS THAT LEFT THIS SCHEMA, recorded here because the refusal that
    // answers each of them is the unknown-key arm in read_settings_file below,
    // and a reader looking for the arm that used to be here needs to find the
    // reason instead. NONE of them is deprecated or tolerated: a `.settings`
    // still carrying one is load-fatal in BOTH products by the ordinary
    // unknown-key rule, hand-editing the line out is the whole recovery, and
    // there is no migration tool and no reader leniency — the architect updates
    // his own files, by his explicit instruction.
    //
    // `font_size` (architect approval 2026-08-01): the GUI-wide monospace text
    // size, deleted with the monospace face itself in row 7 of the kdenlive
    // redesign. Every surface in the product sizes on gui_scale now.
    //
    // `playback_speed` (architect approval 2026-08-27): RETIRED WHOLE. It was
    // the audition's tenths-preset speed multiplier, and the architect runs 1.0
    // everywhere in his one live project — so the key, its preset vocabulary
    // (the deleted playback_speed_presets.h) and the GUI's variable-speed
    // playback machinery went together, leaving the render body's plain
    // source-to-output RATE ratio behind.
    //
    // `gui_scale` (architect approval 2026-08-27): MOVED, not retired. The
    // GUI's rendering scale is a fact about the PANEL in front of the user
    // rather than about the piece, and the two devices that run this product
    // want different values for the same project — so it lives in the GUI's own
    // per-device config now (`$XDG_CONFIG_HOME/warptempo_gui/config`,
    // src/gui/device_config.h), which also took over the RANGE this schema
    // used to own — [50, 400] at the move, [50, 350] since 2026-08-29, when
    // the architect brought the ceiling down (400 was never needed, and at
    // eight lanes 350 keeps the stack inside a 1080-tall window). The ONE
    // owner is is_gui_scale_percent there, called by that file's reader and
    // by the settings editor's red-flash alike, so this sentence names no
    // bracket of its own. The CLI never read the key.
    // (architect approval 2026-08-29, comment only)
    //
    // `audio_player` (architect approval 2026-08-27): MOVED beside gui_scale
    // and for the same reason — which player binary exists is a fact about the
    // DEVICE. The laptop launched audacious on `l`; the tablet had nothing
    // spawnable and carried the blank no-player opt-out, which is exactly the
    // semantics this schema used to load. The device config took the
    // free-UTF-8-verbatim rule with it — AND THEN RETIRED THE KEY WHOLE ON
    // 2026-08-28 (architect approval 2026-08-28, comment-only): bare `l` opens
    // an in-app render player that decodes a wav and plays it through the
    // product's own engine on both devices, so there is no player binary to
    // name anywhere. The key, its blank opt-out, the settings editor's arm and
    // the spawn itself are deleted, and the device config is FIVE keys —
    // `gui_scale`, `projects_repo`, `projects_path`, `last_project`,
    // `sync_path` — in the writer's own order, having been four from that
    // retirement until `sync_path` joined on 2026-08-30 to name the folder
    // Synchronize to external storage mirrors into (architect approval
    // 2026-08-30, comment only). It was
    // never the CLI's key in either home.
    //
    // `projects_repo` (architect approval 2026-08-27, the fifth grant on this
    // file): MOVED beside the two above and for the model's reason — ONE user,
    // ONE repository, so the projects home is a fact about the device the
    // GitHub recheck runs on, not about each piece. The device config took the
    // free-text arm verbatim, its rationale included (the recheck normalizes
    // it against the clone's own `origin`; blank never matches). It had been
    // this schema's key from 2026-08-03 to 2026-08-27.
    //
    // The four render-environment `*_hash` attestation keys (architect approval
    // 2026-08-09) left under the same rule; their record is at
    // kCanonicalSettingsKeys above.
    if (key == "waveform_magnification_level") {
        // THE WAVEFORM'S VISUAL MAGNIFICATION — the count of doublings on
        // the ladder in settings_file.h, and the PICTURE'S alone: the gain it
        // stands for scales the peaks the GUI draws (plate and overview strip
        // both) and reaches no sample, no playback path and no render. The CLI
        // parses it here and never reads it, exactly as it never reads
        // gui_scale.
        //
        // One canonical spelling per value: plain digits through
        // parse_authored_frame (no sign, point, or leading zeros — exactly the
        // writer's %d output), then the RANGE through the shared predicate. A
        // range rather than a membership list, because the value is now a
        // count: every whole number the bracket admits is a state the GUI can
        // produce, and nothing outside it is.
        // (architect approval 2026-08-26 — the settings/parser grant this key
        // landed under, extended the same day to the first retune of it, and
        // again 2026-08-27 to the second.)
        int64_t v = 0;
        if (!parse_authored_frame(value, v) || !is_waveform_magnification_level(v))
            return err("must be an integer in [0, 4] in canonical spelling");
        out.i64 = v;
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
        } else if (key == "waveform_magnification_level") {
            // Range-checked into [0, kWaveformMagnificationLevelMax] by
            // validate_gui_setting above, so the narrowing to int is exact
            // (architect approval 2026-08-26).
            out.waveform_magnification_level = static_cast<int>(gv.i64);
        }
        return {};
    });
    if (!scan) return std::unexpected(std::move(scan.error()));
    return out;
}
