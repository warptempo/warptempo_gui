#pragma once

#include "failure.h"

#include <charconv>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

// THE DEVICE CONFIG — the preferences that describe the MACHINE rather than the
// piece (architect 2026-08-27). Thirteen keys live here and nowhere else,
// EVERY ONE REQUIRED (architect 2026-10-12, his "yes" 2026-10-14; the
// strictness paragraph below):
//
//   gui_scale=<percent>      the GUI's one scale axis, an integer [50, 1000]
//   projects_repo=<host/path> the repository that is the PROJECTS HOME — the
//                            GitHub recheck's corpus; free text, may be blank
//   projects_path=<path>     the ABSOLUTE folder whose subfolders are the
//                            projects (project_model.h owns the model); the
//                            first-run value the defaults file's literal
//                            path, the tablet's external files dir's
//                            `projects/`
//   last_project=<name>      the folder NAME opened last, written at every
//                            successful open; the defaults file's until the
//                            first (blank means nothing opened yet), a name
//                            the device lacks falling through to the first
//                            valid project (startup_source, project_model.h)
//   true_colors=<on|off>     THE sRGB -> DISPLAY-P3 CONVERSION'S SWITCH
//                            (architect 2026-10-21; the ruling at
//                            display_transform.h's head): `on` or `off`, the
//                            two lowercase words and nothing else, any other
//                            word the launch's hard fail; REQUIRED, read at
//                            launch to seed the switch and applied LIVE at
//                            the settings editor's commit from the settings
//                            page's value list's tap (the apply shape,
//                            GuiInputHandler::apply_true_colors); the
//                            first-run value the defaults file's (THE
//                            FIRST-RUN VALUES below)
//   waveform_grow=<px>       THE WAVEFORM PLATE'S GROW (architect
//                            2026-10-31; the rule at render.h's
//                            kWaveformSubcells, the plate shader's GROW,
//                            gl_canvas.cpp): how far the antialiased
//                            waveform's shape is grown on every side before
//                            its coverage is taken, in DEVICE PX, a multiple
//                            of 0.25 in [0, 2] in canonical spelling
//                            (parse_waveform_grow below: `0.5`, `0.75`,
//                            `1`, `1.25` — the integer part, then the
//                            fraction only when it is not zero, with no
//                            trailing zero), held as QUARTERS, one subcell of
//                            the 4x grid each; live at the settings editor's
//                            commit from the settings page's value list's tap
//                            (GuiInputHandler::apply_waveform_grow, a
//                            repaint: the tips do not move). PERMANENT and
//                            REQUIRED: the plate is the one thing on the
//                            canvas not scaled with gui_scale — its bars
//                            are counted in device px (one at the working
//                            zoom, render.h's THE PLATE'S BARS) and its
//                            quarter-row tips are device px, while its
//                            lines are one W (the
//                            unit, render.h's waveform_line_px) — so the
//                            waveform needs one tuning key of its own, and the grow
//                            is the appropriate one, kept for good because a
//                            replacement panel may not come at this density
//                            (the grow is the device-px number whose right
//                            value depends most on it). The first-run value
//                            the defaults file's (1 there, his ruling of
//                            2026-11-04 night at parse_waveform_grow)
//   noise=<name>             THE BACKGROUND SOUND'S FOLDER (architect
//                            2026-10-28; noise.h owns the sound): the name of
//                            one folder under `noise/` beside this file,
//                            exactly one non-empty path component
//                            (is_noise_folder_name below); live at the
//                            settings editor's commit from the settings
//                            page's value list's tap, the resident
//                            recordings freed and the new folder loaded
//                            where the level is audible
//                            (GuiNoise::change_folder); the list shows the
//                            folders there. PERMANENT and
//                            REQUIRED; the first-run value the defaults
//                            file's (`rain` there). A name
//                            with no folder LOADS and greys the NOISE case
//                            (the absent-folder rule, noise.h) — only the
//                            shape is the grammar's
//   noise_level=<dB>         THE BACKGROUND SOUND'S LEVEL AND ITS ONE SWITCH
//                            (architect 2026-10-28; the switch 2026-11-05,
//                            "the only off switch is -inf"; noise.h owns
//                            the sound): a decimal dB in [-60.0, 0.0] or
//                            `-inf` (THE LEVEL GRAMMAR below), applied to
//                            the files as they are — one level, never
//                            ducked; above -inf the bed plays, from the
//                            launch on, and -inf is off, the recordings
//                            freed; live at every motion of the top icon
//                            row's NOISE case (RedesignButton::IconNoiseLevel;
//                            GuiNoise::set_level_db), its ONE ROAD, no
//                            Settings row (the main-page rule,
//                            kSettingsEditorKeys, 2026-11-05 night); the
//                            bed sounds at this level PLUS the attenuation
//                            below, the case printing this number alone;
//                            PERMANENT and REQUIRED, device-level (he
//                            retunes it by hand per piece); the first-run
//                            value the defaults file's — the tablet's own
//                            level as snapshotted, so a fresh install
//                            sounds the bed at it (THE FIRST-RUN VALUES
//                            below)
//   noise_attenuation_level=<dB> THE BACKGROUND SOUND'S FIXED ATTENUATION
//                            (architect 2026-11-05 night: "then
//                            the slider maintains its current behavior and
//                            law"): a dB ADDED UNDER the NOISE case's number
//                            — the bed plays at noise_level +
//                            noise_attenuation_level, the case's face, law,
//                            travel, floor and tap untouched — a
//                            decimal dB in [-60.0, 0.0], THE LEVEL GRAMMAR
//                            LESS `-inf` (parse_noise_attenuation_db below:
//                            the bed's one off switch is noise_level's);
//                            live at the settings editor's Enter, the gain
//                            recomputed and the switch untouched
//                            (GuiNoise::set_attenuation_db); a line-editor
//                            row, no value list; PERMANENT and REQUIRED;
//                            the first-run value the defaults file's
//   noise_crossfade_s=<s>    THE NOISE'S CROSSFADE between two segments, in
//                            whole SECONDS (architect 2026-10-28): an integer
//                            in [1, 30] in canonical spelling, live at the
//                            settings editor's commit from the settings
//                            page's value list's tap (read at the next
//                            transition); PERMANENT and REQUIRED — the
//                            crossfade is a recording's relationship to
//                            itself, so a later sound may want another
//                            value; the first-run value the defaults file's
//   osk_key_w=<px>          THE ON-SCREEN KEYBOARD'S KEY BOX (architect
//   osk_key_h=<px>           2026-10-29, the compact keyboard: "small,
//                            centered, like a phone … chiclets, taller than
//                            wide", the FLAGS sheet's 48 x 60 device px):
//                            the painted keyboard's one-unit key's width
//                            and every key's height IN W (scaled_px's
//                            axis), each an integer in [12, 80] in canonical
//                            spelling (parse_osk_key_size below), no
//                            taller-than-wide constraint (his to tune),
//                            live at the settings editor's Enter, the window
//                            repainted at once
//                            (GuiInputHandler::apply_osk_key_size); TWO
//                            TUNING KEYS with Settings rows, REQUIRED like
//                            every key while they stand, which settle into
//                            constexprs once he rules the numbers
//                            (onscreen_keyboard.h's geometry), their keys,
//                            grammar and rows going then; the first-run
//                            values the defaults file's
//   osk_font_px=<px>         THE ON-SCREEN KEYBOARD'S CAP SIZE (architect
//                            2026-10-29 night, "font size osk config tmp
//                            key"): the keys face's em (gui_font.h's
//                            GuiFace::Keys) IN W, scaled with gui_scale
//                            like every table size, an integer in [6, 24]
//                            in canonical spelling
//                            (parse_osk_font_px below), live at the settings
//                            editor's Enter, the window repainted at once
//                            (GuiInputHandler::apply_osk_font_px); A TUNING
//                            KEY with a Settings row, REQUIRED like every key
//                            while it stands, which settles into the face
//                            table's constexpr once he rules the number
//                            (kGuiFaceSizes), its key, grammar and row going
//                            then; the first-run value the defaults file's
//
// THAT IS THE WRITER'S ORDER and it is the architect's own (2026-08-30):
// the scale and the three of the projects first (gui_scale, projects_repo,
// projects_path, last_project), then the display keys (`true_colors`,
// `waveform_grow`), then the noise's four — folder, level, attenuation,
// crossfade — and the tuning keys at the end (`osk_key_w`, `osk_key_h`,
// `osk_font_px`). The list above is this file's telling of it and
// kDeviceConfigKeys (device_config.cpp) is the one the program emits from.
//
// WHY IT EXISTS. The scale is the panel's business, not the piece's: in the
// sidecar it would be a fact about the PIECE, and it follows the panel
// (architect 2026-08-27). THE PROJECTS' THREE KEYS are the device's too
// (architect 2026-08-27, with the project model): where the projects live is
// a fact about the device (the app's external files dir's `projects/` on the
// tablet); which repository is the projects home is ONE user's ONE repo, not
// a property of each piece, so `projects_repo` is no sidecar key (architect
// approval 2026-08-27); and what is opened last is what lets the tablet,
// which has no command line, open the right piece without a picker at
// startup. NO TOLERANCE FOR A RETIRED KEY (closed_questions.md): a config
// still carrying a key this list does not hold meets the ordinary
// unknown-key first-error refusal naming its line, with no migration and no
// special arm; the sync script's setup removes such a line on the tablet
// (its list of the retired keys is the one record of them).
// The sidecar schema keeps everything that is about the music
// (settings_file.h).
//
// WHERE IT LIVES: `$XDG_CONFIG_HOME/warptempo_gui/config`, falling back to
// `$HOME/.config/warptempo_gui/config` — the same resolution shape the render
// cache uses for XDG_CACHE_HOME (render_cache.cpp), and ONE resolver with no
// `#ifdef` in it: the Android backend points XDG_CONFIG_HOME at the app's
// private internal directory before anything reads it (android_main,
// platform_android.cpp), exactly as it already does for the cache home.
//
// THE FIRST-RUN VALUES ARE ONE TRACKED FILE, `assets/device_config_defaults`
// (architect 2026-11-08: "there should be a pipeline in place
// that snapshots it each time we do an arc … so my settings are durable and
// git-historied"; the design the planner's on his "I defer to you"): THIS
// FILE'S OWN GRAMMAR AND KEY ORDER, all thirteen lines, last_project and
// projects_path included, its values the tablet's own config as
// snapshotted — tools/snapshot_device_defaults.sh refreshes it from the
// tablet at every arc's last commit, and a changed file rides that commit.
// The APK packs it (apk/build_apk.sh's assets step) and the backend's
// template is that asset PARSED BY THIS FILE'S READER
// (parse_device_config_text below, GuiPlatform::device_config_defaults), so
// a defaults file that breaks the grammar fails the launch exactly as a bad
// config does: the file is the program's own, a bad one a program bug. No
// value in the source is a first-run value: the struct's members and the
// seats' initializers (render.cpp) are CONSTRUCTION STATE, overwritten by
// the startup read before any use. The sync script's setup adds the same
// file's lines where a tablet's config lacks a key (its ADDED_LINES,
// derived from the file at run time). A TUNING KEY STAYS A KEY: a snapshot
// is not a retirement.
//
// THE STRICTNESS POSTURE IS THE SIDECAR'S, DELIBERATELY. The file is
// program-written — the first run stamps it from the defaults file and
// every later commit rewrites it — so any violation is a hand edit, which the
// two-category rule makes ADVERSARIAL: whole-file schema, EXACTLY the
// thirteen keys and each at most once, EVERY KEY REQUIRED (architect
// 2026-10-12, deferred, his "yes" 2026-10-14: "if one of these lines is not
// in place, it means something's wrong with the config or the setup" — the
// writer emits every line and the defaults file spells every key, so an absent
// line is the shared scanner's missing-required-key refusal like any other,
// with no migration and no launch-time repair),
// one canonical spelling per
// value, and the FIRST error is fatal at startup with a blunt terminal line
// naming the path and the offending line. No repair, no partial apply, no
// silent fallback to defaults — a fallback would silently discard a value the
// user typed. The lexing itself is not respelled here: the shared scanner
// (warptempo_settings::scan_key_value_file, settings_file.h) owns "split at the
// first '=', no blank/comment/whitespace tolerance, no duplicate key, every
// required key present", and this file owns only the per-key grammar over it.
//
// ORDER IS THE WRITER'S, NOT THE READER'S — the sidecar's own posture again.
// The key list in device_config.cpp is the EMITTED order (the head's list)
// and it is what every file this program writes carries; the shared scanner checks
// MEMBERSHIP, duplicates and presence and never position, so a hand-reordered
// file still loads. That costs nothing and buys the sidecar's symmetry: there,
// kSettingsOrder owns the write order and the reader has always been
// order-insensitive. A position rule would be a second guard for a state the
// writer cannot produce.
//
// NO UNDO, NO DIRTY, NO Ctrl+S. Every key is history-less, committed through
// its own chokepoint, and each chokepoint writes this file immediately, so the
// file is always the last committed state and a save carries none of them.
// The chokepoints are the callers inventory at write_device_config below.
//
// EVERY EDITABLE KEY HAS AN IN-APP ROAD (architect 2026-09-02): THE
// SETTINGS PAGE's row (settings_editor.h) — its value list, or the line
// editor holding the value alone (architect 2026-11-19) — for
// `gui_scale`, `projects_repo`, `projects_path`, `true_colors`,
// `waveform_grow`, `noise`, `noise_attenuation_level`, `noise_crossfade_s`
// and the tuning keys `osk_key_w`, `osk_key_h` and `osk_font_px`
// (kSettingsEditorKeys) — Tab recalling a key's current value and
// completing the path key's folders, each committed through this file's
// writer under the key's own grammar below. THE LEVEL,
// `noise_level`, is edited on the main page alone, the top icon row's NOISE
// case, whose lift writes through the same writer
// (architect 2026-11-05 night: a key shown and edited on the main page gets
// no Settings row, kSettingsEditorKeys). A HAND EDIT UNDER A RUNNING APP IS
// CLOBBERED BY THE NEXT IN-APP COMMIT: the live struct is the truth and
// every commit rewrites the whole file from it, ordinary behaviour for a
// program-written config; the in-app road is the sanctioned one and the
// hand edit is the quit-first
// alternative. `last_project` is the one key with no editor that the program
// writes — it is the program's own.

// The whole file, typed. The member defaults are CONSTRUCTION STATE, not load
// fallbacks, for every key: each is required, so a successful read always
// assigns them, and NONE IS A FIRST-RUN VALUE: the backend's template
// (GuiPlatform::device_config_defaults) is the shipped defaults file parsed
// (THE FIRST-RUN VALUES above), every key spelled there
// (architect 2026-10-14: every key required). EVERY KEY
// WRITES ITS LINE, so a first run stamps the
// whole file and its read-back loads. (No key names the face: Liberation
// Sans is the one, gui_font.h.)
//
// ONE OF THEM MEANS SOMETHING BY BEING EMPTY, saying so in its own grammar
// below: `last_project` empty is "nothing opened yet". (`projects_repo` also
// ADMITS empty, being free text, but empty is just a value there — one that
// never matches a remote.)
// The key is still REQUIRED in every case: the file admits no absent state,
// only an empty VALUE.
//
// The members are in the writer's order. (No key names a color: every
// color is Arc-Dark's, the one compiled row kArcDarkTones, gui_theme.h;
// architect 2026-11-21.)
struct DeviceConfig {
    int         gui_scale = 138;
    std::string projects_repo;
    std::string projects_path;
    std::string last_project;
    // TRUE COLORS (2026-10-21, the head's list): required; `on` the
    // construction state.
    bool        true_colors = true;
    // THE WAVEFORM PLATE'S GROW in quarters of a device px (2026-10-31, the
    // head's list; a permanent key): required; the construction state is
    // 1 px (kWaveformGrowDefaultQuarters below).
    int         waveform_grow_quarters = 4;
    // THE NOISE'S FOLDER NAME, its level in dB (a double, -inf off, THE
    // LEVEL GRAMMAR, 2026-11-05), its fixed attenuation in dB under that
    // level (a double, never -inf, 2026-11-05 night) and its crossfade in
    // seconds (2026-10-28, the head's list): required; the construction
    // state is `rain`, -inf, -9 and 4.
    std::string noise                   = "rain";
    double      noise_level             = -std::numeric_limits<double>::infinity();
    double      noise_attenuation_level = -9.0;
    int         noise_crossfade_s       = 4;
    // THE ON-SCREEN KEYBOARD'S KEY BOX in W (2026-10-29, the head's list;
    // two tuning keys): required; the construction state is 24 x 30
    // (kOskKeyWDefaultW, kOskKeyHDefaultW below).
    int         osk_key_w         = 24;
    int         osk_key_h         = 30;
    // THE ON-SCREEN KEYBOARD'S CAP SIZE, its em in W (2026-10-29 night, the
    // head's list; a tuning key): required; the construction state is 12
    // (kOskFontPxDefaultW below).
    int         osk_font_px       = 12;
};

// THE PROJECTS REPOSITORY AS CONSTRUCTED — AppState's `projects_repo`
// initializer, its one reader, a value gui_main's session seat overwrites
// from the live config before anything reads the field (main.cpp, "THE
// DEVICE CONFIG'S LIVE VALUES"). IT IS NOT THE FIRST-RUN VALUE: that is the
// defaults file's `projects_repo=` line (THE FIRST-RUN VALUES above),
// which names the same repository. It is NOT a load fallback — the key is required, so a
// config either names a repository (blank included) or refuses. THE PROJECTS
// REPOSITORY IS ITS OWN, SEPARATE AND PUBLIC (architect 2026-09-27): the
// pieces' sidecar history lives in warptempo_projects, laid out as
// `projects/<piece>/` inside it, and this program's own repository ignores
// `projects/` (the walk's match, is_piece_sidecar_path, history_diff.h).
inline constexpr const char* kDefaultProjectsRepo =
    "github.com/warptempo/warptempo_projects";

// THE gui_scale RANGE — the ONE owner, beside the key (architect approval
// 2026-08-27). Both askers call it
// and neither respells the bracket: this file's reader, and the settings
// editor's `gui_scale=` refused commit ("loadable iff it commits", the
// standing rule).
//
// THE PERCENT IS DEVICE PX PER W, ONE ARC PX (the unit's statement is at
// render.h's scaled_px): the tablet runs 200 (a 16-W glyph at 32 device px,
// 16 · 2); 100 is one device px per W; 1000 is the ceiling; 50 is the
// half-size floor,
// which is where every structural dimension in render.h's scaled_px
// accessors still has a floor holding it above zero.
//
// THE CEILING IS 1000 (architect 2026-10-05). It is a vocabulary, not a
// fit: the lanes above the waveform — the title bar, the top icon row over
// Arc's toolbar pads and its case, the trim lane, the ruler and the marker
// lane and their lines, one W each — and below it — the line under the
// canvas, the bottom icon row and the status bar — scale with gui_scale
// (main.cpp's lane table and its stacks' record); whether they pass the
// tablet's 1440 rows short of the ceiling turns on the scale, and where
// they do the waveform is the zero height waveform_area's floor answers (the
// arithmetic and the guard at waveform_area, main.cpp).
//
// THE LAYOUT IS NOT WIDENED WITH THE CEILING, deliberately: past a width or a
// scale the top icon row — its left runs and its one right run — no longer
// fits, and a left-run
// member that would end past the right run (less a run gap) is dropped
// whole, NOISE first, then the third run's keys from its tail; no key is cut
// and the right run stands whole (the overflow rule at kIconRowViewPairs,
// paint_handler.cpp). Where that is — the window widths
// and the scales the tablet's 2304-px panel holds the row whole at, the
// key's width (render.h's kKeyWidthW) and the run gap (kRunGapW) moving it
// and the key's height none of it — is THE FIT at paint_icon_row's width math (paint_handler.cpp),
// its one statement. There is no
// collision rule anywhere: the crop-at-the-floor allowance recorded at
// kMinWindowWidthPx (render.h) is the standing answer for a narrow window. A
// scale is a VOCABULARY; which of its values lays out well is the
// architect's call on his own panel, not a validator's.
inline constexpr bool is_gui_scale_percent(int64_t v) {
    return v >= 50 && v <= 1000;
}

// The scale's reason, spelled once for its two readers (the config reader's
// `bad_value` line and the settings editor's card).
inline constexpr const char* kGuiScaleGrammarReason =
    "must be an integer in [50, 1000] in canonical spelling";

// THE ASCII WHITESPACE SET this file's grammars refuse at a value's edges —
// all six of it, spelled as a byte set rather than asked of the locale, which
// no other grammar surface in the product consults either. ONE owner, TWO
// askers below: the path value grammar and the project name's.
inline constexpr bool is_config_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}

// THE PATH VALUE GRAMMAR — the ONE owner for the path key (`projects_path`,
// below): non-empty, ABSOLUTE, carrying NO LINE
// SEPARATOR anywhere (`\n` or `\r`) and NO ASCII WHITESPACE at either edge,
// and taken verbatim otherwise. A path may legitimately hold a space in the
// middle of a component, so only the edges are refused.
//
// THE TWO LEXICAL CLAUSES ARE `last_project`'s, for its reasons said of a path
// (the full argument is at is_last_project_name): the file is line-based
// `key=value` with NO ESCAPING, so a value carrying a line separator cannot be
// serialized at all, and an edge space is serializable but not canonical — and
// it would compose a DIFFERENT folder than the one the hand meant to name,
// silently. `\r` HAS A REAL PRODUCER:
// std::getline strips the `\n` of a CRLF line and leaves the `\r` on the
// value, so a config saved by a Windows editor refuses out loud here instead
// of aiming an act at `<path>\r`.
//
// EXISTENCE IS DELIBERATELY NOT A GRAMMAR QUESTION: a `projects_path` that
// names no folder is startup's own "No project under <projects_path>" answer
// (project_model.h) — the config is right about WHERE, and the thing may
// simply not be there yet.
inline bool is_config_path_value(const std::string& v) {
    if (v.empty()) return false;
    if (v.find('\n') != std::string::npos) return false;
    if (v.find('\r') != std::string::npos) return false;
    if (is_config_whitespace(v.front()) || is_config_whitespace(v.back()))
        return false;
    return std::filesystem::path(v).is_absolute();
}

// THE projects_path GRAMMAR — the path value grammar exactly, this key having
// no empty form: the app always has a projects path.
inline bool is_projects_path(const std::string& v) {
    return is_config_path_value(v);
}

// THE GRAMMARS' REASONS, spelled once beside the predicates they explain:
// the config reader's `bad_value` line and the settings editor's refusal
// card are the two readers of each, so a refusal says the same words on the
// terminal at startup and on a card at the commit. THE PATH REASONS NAME THE
// EDGE RULE because that is the fault a hand actually makes: an absolute path
// with a trailing space (or the `\r` a CRLF-saved file leaves on every value)
// is refused, and "must be an absolute path" alone would read as a lie
// against a value that plainly is one.
inline constexpr const char* kProjectsPathGrammarReason =
    "must be an absolute path with no whitespace at either edge";
inline constexpr const char* kProjectsRepoGrammarReason =
    "must not carry a line separator";

// THE projects_repo GRAMMAR — free text, verbatim, EMPTY INCLUDED (a blank
// repository never matches a remote and so disables the GitHub recheck), with
// ONE refusal: a line separator anywhere (`\n` or `\r`), the one thing this
// line-based `key=value` file with no escaping cannot carry — the writer would
// emit more than one physical line and the shared scanner would refuse the
// remainder on the next read. No host/path grammar is enforced here: the
// recheck normalizes the value against the local clone's own `origin` and
// refuses the mismatch there, which is a far better place to judge it than a
// reader that cannot see the clone. The `\r` clause has the same real
// producer the path key's has (std::getline on a CRLF-saved file); the
// editor's own filter drops control bytes, so the only value the clause
// refuses is a hand-edited one.
inline bool is_projects_repo(const std::string& v) {
    if (v.find('\n') != std::string::npos) return false;
    if (v.find('\r') != std::string::npos) return false;
    return true;
}

// THE true_colors GRAMMAR (2026-10-21) — the two lowercase words, exactly;
// the one owner the reader, the writer, the settings editor's commit and its
// recall read (format_true_colors, parse_true_colors), and the words its
// value list shows (kTrueColorsWords).
inline constexpr const char* kTrueColorsOn  = "on";
inline constexpr const char* kTrueColorsOff = "off";
inline constexpr const char* kTrueColorsWords[] = {kTrueColorsOn,
                                                   kTrueColorsOff};
inline constexpr const char* kTrueColorsGrammarReason = "must be on or off";
inline const char* format_true_colors(bool on) {
    return on ? kTrueColorsOn : kTrueColorsOff;
}
inline std::optional<bool> parse_true_colors(std::string_view v) {
    if (v == kTrueColorsOn)  return true;
    if (v == kTrueColorsOff) return false;
    return std::nullopt;
}

// A CANONICAL SIGNED INTEGER (the shared lexis of the noise's crossfade and
// the on-screen keyboard's key box and cap size): an optional `-`,
// then digits with no leading zero, and no `-0` — exactly what `%d` prints,
// so the reader admits back only the writer's spelling. At most nine digits,
// which every range below sits far inside, so the accumulation cannot
// overflow. The one lexer the integer grammars read; each owns its own
// range.
inline std::optional<int> parse_canonical_config_int(std::string_view v) {
    bool neg = false;
    if (!v.empty() && v.front() == '-') {
        neg = true;
        v.remove_prefix(1);
    }
    if (v.empty() || v.size() > 9) return std::nullopt;
    if (v.size() > 1 && v.front() == '0') return std::nullopt;
    if (neg && v == "0") return std::nullopt;
    int n = 0;
    for (const char c : v) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + (c - '0');
    }
    return neg ? -n : n;
}

// THE LEVEL GRAMMAR — `noise_level` AND, LESS `-inf`,
// `noise_attenuation_level`, ONE OWNER (architect 2026-11-05: "i plan to
// use stable values for both"; THE EXACT DOUBLE, architect 2026-11-05
// night — the face shows its own short spelling, the file keeps the value
// whole): the value is `-inf`,
// or a DECIMAL dB in [-60.0, 0.0] — an optional `-`, digits with no leading
// zero but a lone `0`, then optionally `.` and ONE OR MORE digits; no `+`,
// no exponent, no leading `.`. THE WRITER SPELLS THE SHORTEST DECIMAL THAT
// ROUND-TRIPS, in fixed notation — "0", "-9", "-15.6",
// "-0.0108695652173913", "-60" — and `-inf` (format_level_db); THE READER
// ADMITS ANY FRACTIONAL LENGTH (parse_level_db), so "0.0" and "-9.0" load
// as they are and the next commit
// rewrites them canonical; the value is std::from_chars' on the checked
// text, the range checked on the value; "-0", "-0.0", "-0.00…" are refused
// (zero is unsigned), as every other spelling is. THE FACE PRINTS ONE
// DECIMAL UNDER 10 dB OF MAGNITUDE AND NONE FROM 10 ON (format_level_db_face,
// architect 2026-11-19: "0.0", "-9.5", "-22", "-60", "-inf"), the top icon
// row's NOISE case's number alone. THE RANGE IS THE
// SLIDER'S FLOOR: the case reads -60 at its last step and -inf below it
// ("-60 then -inf"; the taper at level_db_from_travel, app_state.h). -inf
// is the infinity (kLevelDbOff), the noise's one off switch (noise.h's
// head); THE ATTENUATION refuses it
// (parse_noise_attenuation_db: the bed's switch is noise_level's alone).
// The readers: the config's reader and writer, the case's lift (the
// drag persists through the writer) and its painter (the face's
// formatter); the attenuation's line-editor arm and its recall
// (apply_device_setting, recall_gui_setting_value). 0 dB is the recordings
// as they are; the project voice plays at 0 dB, summed unscaled with the bed
// before the clip (playback_common.h's head), and the tablet's own volume
// sets the loudness.
inline constexpr double kLevelDbMin = -60.0;
inline constexpr double kLevelDbMax = 0.0;
inline constexpr double kLevelDbOff = -std::numeric_limits<double>::infinity();
inline constexpr const char* kLevelGrammarReason =
    "must be -inf or a decimal in [-60.0, 0.0]";
// THE FILE'S SPELLING (the shortest round-trip decimal, fixed; `-inf`) and
// THE FACE'S (one decimal under 10 dB of magnitude, none from 10; `-inf`),
// device_config.cpp.
std::string format_level_db(double db);
std::string format_level_db_face(double db);
inline std::optional<double> parse_level_db(std::string_view v) {
    if (v == "-inf") return kLevelDbOff;
    const std::string_view text = v;
    bool neg = false;
    if (!v.empty() && v.front() == '-') {
        neg = true;
        v.remove_prefix(1);
    }
    // The integer part: digits, no leading zero but a lone `0`, at most
    // three (the range's widest, 60, sits far inside).
    const size_t dot = v.find('.');
    const std::string_view whole = v.substr(0, dot);
    if (whole.empty() || whole.size() > 3) return std::nullopt;
    if (whole.size() > 1 && whole.front() == '0') return std::nullopt;
    bool nonzero = false;
    for (const char c : whole) {
        if (c < '0' || c > '9') return std::nullopt;
        if (c != '0') nonzero = true;
    }
    // The fractional part, when present: one or more digits.
    if (dot != std::string_view::npos) {
        const std::string_view frac = v.substr(dot + 1);
        if (frac.empty()) return std::nullopt;
        for (const char c : frac) {
            if (c < '0' || c > '9') return std::nullopt;
            if (c != '0') nonzero = true;
        }
    }
    if (neg && !nonzero) return std::nullopt;   // no "-0", "-0.0", "-0.00…"
    double db = 0.0;
    const auto res = std::from_chars(text.data(), text.data() + text.size(),
                                     db, std::chars_format::fixed);
    if (res.ec != std::errc{} || res.ptr != text.data() + text.size())
        return std::nullopt;
    if (db < kLevelDbMin || db > kLevelDbMax) return std::nullopt;
    return db + 0.0;
}
// THE NOISE'S ATTENUATION (architect 2026-11-05 night, the head's list):
// THE LEVEL GRAMMAR LESS `-inf` — a decimal dB in [-60.0, 0.0], the same
// spellings, the same writer (format_level_db, which never meets -inf
// here). Its first-run value the defaults file's (THE FIRST-RUN VALUES
// above).
inline constexpr const char* kNoiseAttenuationGrammarReason =
    "must be a decimal in [-60.0, 0.0]";
inline std::optional<double> parse_noise_attenuation_db(std::string_view v) {
    if (v == "-inf") return std::nullopt;
    return parse_level_db(v);
}

// THE noise_crossfade_s GRAMMAR (architect 2026-10-28) — an integer in
// [1, 30] SECONDS in canonical spelling, the one owner the same five read
// (format_noise_crossfade_s, parse_noise_crossfade_s; the value list's 30
// rows are this range in whole seconds). Seconds because the crossfade is a
// recording's relationship to itself, chosen per sound and never finer than
// a second; the voice converts at its one setter (GuiNoise::set_crossfade_s,
// noise.h).
inline constexpr int kNoiseCrossfadeMinS = 1;
inline constexpr int kNoiseCrossfadeMaxS = 30;
inline constexpr const char* kNoiseCrossfadeGrammarReason =
    "must be an integer in [1, 30] in canonical spelling";
std::string format_noise_crossfade_s(int s);
inline std::optional<int> parse_noise_crossfade_s(std::string_view v) {
    const std::optional<int> n = parse_canonical_config_int(v);
    if (!n || *n < kNoiseCrossfadeMinS || *n > kNoiseCrossfadeMaxS)
        return std::nullopt;
    return n;
}

// THE osk_key_w AND osk_key_h GRAMMAR (architect 2026-10-29), ONE FOR THE
// TWO — an integer in [12, 80] W in canonical spelling (24 to 160 device px
// on the tablet), the one owner
// the reader, the writer, the settings editor's commit and its recall read
// (format_osk_key_size, parse_osk_key_size). kOskKeyWDefaultW and
// kOskKeyHDefaultW ARE CONSTRUCTION STATE ALONE, render.cpp's seats'
// initializers, overwritten by gui_main's startup seeding before the first
// layout; THE FIRST-RUN VALUES ARE THE DEFAULTS FILE'S (THE FIRST-RUN
// VALUES above). Theirs are the FLAGS sheet's key box, 24 W wide and 30
// tall, 48 x 60 device px on the tablet; the range runs from half that width to
// well past the height, wide enough for him to try a key wider than tall,
// and its top keeps the widest row (thirteen units) inside the tablet's
// 2304 device px (13 x (160 + 2) - 2 = 2104 there, the gap the program
// keys' one W, 2 device px on the tablet). The
// settings page opens the LINE EDITOR on each, not a value list (69 values
// is no short domain, kSettingsValueListKeys' rule). TWO TUNING
// KEYS: when he rules the numbers, the keys, these functions and their
// Settings rows go, and the values become constexprs behind
// onscreen_keyboard.h's key_w_px and key_h_px.
inline constexpr int kOskKeySizeMinW  = 12;
inline constexpr int kOskKeySizeMaxW  = 80;
inline constexpr int kOskKeyWDefaultW = 24;
inline constexpr int kOskKeyHDefaultW = 30;
inline constexpr const char* kOskKeySizeGrammarReason =
    "must be an integer in [12, 80] in canonical spelling";
std::string format_osk_key_size(int px);
inline std::optional<int> parse_osk_key_size(std::string_view v) {
    const std::optional<int> n = parse_canonical_config_int(v);
    if (!n || *n < kOskKeySizeMinW || *n > kOskKeySizeMaxW)
        return std::nullopt;
    return n;
}

// THE osk_font_px GRAMMAR (architect 2026-10-29 night) — an integer in
// [6, 24] W in canonical spelling (12 to 48 device px on the tablet), the
// keys face's em (gui_font.h's
// GuiFace::Keys, scaled with gui_scale like every table size), the one
// owner the reader, the writer, the settings editor's commit and its recall
// read (format_osk_font_px, parse_osk_font_px). kOskFontPxDefaultW IS
// CONSTRUCTION STATE ALONE, render.cpp's seat's initializer, overwritten by
// gui_main's startup seeding before the first paint; THE FIRST-RUN VALUE IS
// THE DEFAULTS FILE'S (THE FIRST-RUN VALUES above). Its 12 W is the face
// table's, 24 device px on the tablet (architect 2026-10-24, "C24").
// The range: 6 W, 12 device px, is the smallest em at which a cap stays
// legible on the tablet; 24 W is twice 12 W, the em by which every
// function word but "Bksp" and "Space" already overhangs its key at the
// 24 x 30 key box ("Esc", the last of them, from em 21 W; "Bksp", 118
// device px at em 24 W against the 2.5u key's 122-px face, not until 25), so a larger
// cap would only overhang more (onscreen_keyboard.h's EVERY WORD FITS ITS
// KEY paragraph, cap_word; architect 2026-10-29 night). The settings page
// opens the LINE EDITOR on it, not a value list (19 values is no short
// domain, kSettingsValueListKeys' rule). A TUNING KEY: when he rules the number, the
// key, these functions and its Settings row go, and the value settles into
// kGuiFaceSizes' Keys entry.
inline constexpr int kOskFontPxMinW     = 6;
inline constexpr int kOskFontPxMaxW     = 24;
inline constexpr int kOskFontPxDefaultW = 12;
inline constexpr const char* kOskFontPxGrammarReason =
    "must be an integer in [6, 24] in canonical spelling";
std::string format_osk_font_px(int px);
inline std::optional<int> parse_osk_font_px(std::string_view v) {
    const std::optional<int> n = parse_canonical_config_int(v);
    if (!n || *n < kOskFontPxMinW || *n > kOskFontPxMaxW)
        return std::nullopt;
    return n;
}

// THE waveform_grow GRAMMAR (architect 2026-10-31; quarters of the 4x
// lattice, render.h's kWaveformSubcells) — a
// multiple of 0.25 DEVICE PX in [0, 2] in canonical spelling, held as
// QUARTERS in [0, 8] (kWaveformGrowQuartersPerPx: one quarter is one
// subcell of render.h's 4x grid, kWaveformSubcells, so the number is the
// plate shader's GROW as it is), the one owner the reader, the writer, the
// settings editor's commit and its recall read, and the settings page's
// value list (format_waveform_grow, parse_waveform_grow; the list's 9 rows
// are this range in quarters). THE CANONICAL SPELLING is the shortest exact
// decimal: the integer part, then `.` and the fraction only when it is not
// zero, with no trailing zero — `0`, `0.25`, `0.5`, `0.75`, `1`, `1.25`,
// `1.5`, `1.75`, `2` — and the reader admits exactly the nine strings the
// writer spells (it formats each quarter and compares), so `0.50`, `1.0`,
// `.5` and anything off the quarters lattice or out of range are the
// launch's hard fail, as every other key's off-spelling is.
// kWaveformGrowDefaultQuarters IS CONSTRUCTION STATE ALONE — render.cpp's
// seat's initializer, overwritten by gui_main's startup seeding before the
// window exists, and the plate program gl_canvas.cpp builds first, rebuilt
// at the first draw of a list at another grow; THE FIRST-RUN VALUE IS THE
// DEFAULTS FILE'S (THE FIRST-RUN VALUES above). Its 1 is his ruled value
// (architect 2026-11-04 night, on the real lit capture at grow 1 beside the
// unit's 2-px lines: "is good as well"); the top, 2,
// is what the shader's one-word window was sized for (gl_canvas.cpp's
// asserts) and twice 1.
inline constexpr int kWaveformGrowQuartersPerPx   = 4;
inline constexpr int kWaveformGrowMinQuarters     = 0;
inline constexpr int kWaveformGrowMaxQuarters     = 8;
inline constexpr int kWaveformGrowDefaultQuarters = 4;
inline constexpr const char* kWaveformGrowGrammarReason =
    "must be a multiple of 0.25 in [0, 2] in canonical spelling "
    "(0, 0.25, 0.5, ... 2)";
std::string format_waveform_grow(int quarters);
inline std::optional<int> parse_waveform_grow(std::string_view v) {
    for (int n = kWaveformGrowMinQuarters; n <= kWaveformGrowMaxQuarters; ++n)
        if (format_waveform_grow(n) == v) return n;
    return std::nullopt;
}

// HOW A PATH UNDER THE PROJECTS PATH IS NAMED IN A SENTENCE — the basename
// rule's composer for everything the project model and the loaders say
// (failure.h). A sentence that carries a path names THE PROJECT FOLDER
// AND THE FILE — `550 - 1/07 - Menuetto.settings` — and never the projects
// path itself, because every one of those sentences is one line on a
// notification card that CLIPS, and the leading `/home/.../projects/` is the
// one part of it the reader already knows: it is this file's own key, the
// same for every project on the device. A path with no parent falls back to
// its filename. Lexical, following no link.
//
// IT LIVES BESIDE THE KEY IT REFUSES TO SPELL rather than in the project
// model, so the sidecar readers (settings_io.cpp) can compose the same
// sentence without depending on the model; the model's own refusals name the
// FOLDER NAME alone, which is `folder.filename()` and needs nothing from here.
inline std::string shown_project_path(const std::filesystem::path& p) {
    const std::string parent = p.parent_path().filename().string();
    if (parent.empty()) return p.filename().string();
    return parent + "/" + p.filename().string();
}

// THE last_project GRAMMAR — the ONE owner: EMPTY, or exactly ONE path
// component — no `/`, not `.`, not `..`, no LINE SEPARATOR anywhere in it
// (`\n` or `\r`), and no leading or trailing ASCII WHITESPACE (all six of it,
// through the byte-set owner is_config_whitespace above). The name is
// otherwise verbatim, since a project folder may legitimately end in any
// other character. A separator or a
// dot-name is the ADVERSARIAL class and refuses at load like every other schema
// violation: the program writes this key with one folder NAME, so a `/` or a
// `..` is a state its one producer cannot make — and a name carrying a
// separator would compose a path that LEAVES the folder it claims to name
// (`../other` resolves outside the projects path entirely), after which the
// program would open and EDIT a piece that was never a project here. So the
// composition happens only after the name is known to be a single component,
// and anything else refuses the whole config out loud.
//
// THE LINE SEPARATOR IS REFUSED ANYWHERE, not just at the edges, because the
// file is line-based `key=value` with NO ESCAPING: a name carrying a `\n` or a
// `\r` cannot be serialized at all — the writer would emit more than one
// physical line and the shared scanner would refuse the remainder as "not a
// key=value line" on the next read. The edge-whitespace rule stays exactly as
// it is (a trailing space is serializable but not canonical); this clause is
// about what the FORMAT can carry.
//
// THE GRAMMAR IS ALSO A MEMBERSHIP TEST, not only a reader's: a folder whose
// name this predicate refuses is not a project on any road, because every
// successful open writes that name into this key (enumerate_project_names,
// project_model.{h,cpp}, is the other caller).
inline bool is_last_project_name(const std::string& v) {
    if (v.empty()) return true;
    if (v == "." || v == "..") return false;
    if (v.find('/') != std::string::npos) return false;
    if (v.find('\n') != std::string::npos) return false;
    if (v.find('\r') != std::string::npos) return false;
    if (is_config_whitespace(v.front()) || is_config_whitespace(v.back()))
        return false;
    return true;
}

// THE noise GRAMMAR (architect 2026-10-28) — the ONE owner the reader, the
// settings editor's commit, the settings page's value list (its rows the
// folders under `noise/` this admits) and the sync script's mirror (its own
// respelling, scripts/warptempo_sync's mirror_noise) read: exactly ONE path
// component and
// NON-EMPTY — is_last_project_name's clauses (no `/`, not `.` or `..`, no
// `\n` or `\r` anywhere, no leading or trailing config whitespace) with the
// empty arm REFUSED: the key names a sound, the defaults file spells one, and a
// blank line is a state the program never writes. THE COMPOSITION ARGUMENT IS
// is_last_project_name's: a `/` or a `..` would compose a path outside
// `noise/` (GuiNoise::probe composes the folder only from a name this admits),
// and a line separator cannot be serialized. THE ABSENT FOLDER IS NOT THE
// GRAMMAR'S: a well-shaped name whose folder does not exist LOADS, and the
// NOISE case greys (noise.h's head). CLOSED (closed_questions.md, this its
// owner): an empty `noise=` line. THE CHOICE OF SOUND FROM INSIDE THE APP is
// the settings page's `noise` row (architect 2026-10-28 night): a value list
// of the folders there, a tap committing the name through the device keys'
// one body (apply_device_setting).
inline constexpr const char* kNoiseFolderGrammarReason =
    "must be one folder name under noise/, not empty and not a path";
inline bool is_noise_folder_name(const std::string& v) {
    return !v.empty() && is_last_project_name(v);
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
// hands to the atomic writer, and the first-run template's bytes too (the
// defaults file's lines as this writer spells them back), so the
// stamped file and a committed one are one format by construction.
std::string format_device_config_text(const DeviceConfig& cfg);

// Parse and validate the whole config file at `path` under the schema at the
// head of this file. Errors carry a "line N: " prefix where a line is at fault;
// the caller adds the path context.
std::expected<DeviceConfig, std::string> read_device_config(
    const std::filesystem::path& path);

// THE SAME READER OVER BYTES IN HAND: the shipped defaults
// file's text (assets/device_config_defaults, THE FIRST-RUN VALUES above),
// judged by read_device_config's one body — the shared scanner and every
// per-key grammar — so a defaults file is loadable iff a config holding its
// lines is. Errors carry the same "line N: " prefix; the caller (the
// backend's template, GuiPlatform::device_config_defaults) adds the asset's
// name.
std::expected<DeviceConfig, std::string> parse_device_config_text(
    const std::string& text);

// Write the live values atomically (temp + fsync + rename, through the shared
// atomic writer), creating the parent directory if it is missing. Answers
// nothing on success and THE FAILURE ON ANY I/O FAILURE — both clauses
// composed here at the one failure point (GuiFailure, failure.h: the
// diagnostic with the full path and the system's words where there are any,
// the display naming the file by its basename, `config`), this writer
// printing nothing itself; each caller prints the diagnostic and, where it
// has a card surface, raises the display. A failed persist is advisory, never
// fatal: the live value stands for this session and the next launch reads
// whatever the file last held.
//
// The card stands (architect 2026-09-04): a failed persist is not
// adversarial reach, for each of these writes is a deliberate press whose
// result nothing paints, and the deliberate-press rule (notifications.h) is
// what decides such a case — so the failure says its sentence, on a NORMAL
// card, where the caller has a card surface.
//
// THE LIVE CONFIG HAS ONE OWNER, gui_main's loop (main.cpp): the ONE
// DeviceConfig it reads at startup outlives every project the process opens,
// and AppState reaches it through `AppState::device_config` (a pointer, seated
// at each AppState's construction). That is what lets a value typed in one
// project survive the reopen that tears that AppState down — a failed persist
// is advisory, so re-reading the FILE at each reopen could lose a value the
// user committed in the session — and it is why the callers below write the
// struct they were handed rather than composing one from AppState's fields.
//
// THREE CALL SITES CARRY THE KEY COMMITS, and this is their inventory:
// the scale's chokepoint GuiInputHandler::apply_gui_scale (input_handler.cpp);
// the settings editor's ONE persist (GuiSettingsEditor::persist_device_config),
// which serves its ONE device-key body — ten keys, `projects_repo=`,
// `projects_path=`, `true_colors=`, `waveform_grow=` (its live act
// GuiInputHandler::apply_waveform_grow), `noise=` (its
// GuiNoise::change_folder), `noise_attenuation_level=` (its
// GuiNoise::set_attenuation_db), `noise_crossfade_s=`, and the tuning keys
// `osk_key_w=` and `osk_key_h=` (theirs GuiInputHandler::apply_osk_key_size)
// and `osk_font_px=` (its GuiInputHandler::apply_osk_font_px)
// (GuiSettingsEditor::commit_device_setting, settings_editor.cpp) — and the
// top icon row's level case's end (GuiInputHandler::settle_level_case:
// `noise_level=`, its live act GuiNoise::set_level_db); and
// gui_main's `last_project` write on the success path
// of every open (main.cpp). A same-value commit never reaches any of them —
// each gates the no-op ahead of the write — so a file rewrite means a value
// actually moved. `last_project` is the one key with no editor: it is the
// program's own.
std::optional<GuiFailure> write_device_config(const DeviceConfig& cfg);

// STARTUP: the config, created from `first_run_template` if the file does not
// exist yet and then read back like any other. The template is the running
// BACKEND's answer (GuiPlatform::device_config_defaults): the shipped
// defaults file parsed, every key spelled there, its values the tablet's
// own config as last snapshotted (THE FIRST-RUN VALUES above), so a first run lands
// a file that is already right for it and the user edits from there rather
// than from a wrong guess. A missing parent directory is created.
//
// The error is the whole failure vocabulary of this file: an unresolvable
// config home, a create that failed, an unopenable file, or the reader's first
// schema refusal. Every one of them is FATAL at the caller (gui_main) — the
// blunt terminal line and no window.
std::expected<DeviceConfig, std::string> load_device_config(
    const DeviceConfig& first_run_template);
