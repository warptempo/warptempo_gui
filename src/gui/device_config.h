#pragma once

#include "failure.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

// THE DEVICE CONFIG — the preferences that describe the MACHINE rather than the
// piece (architect 2026-08-27). Five keys live here and nowhere else:
//
//   gui_scale=<percent>      the GUI's one scale axis, an integer [50, 350]
//   projects_repo=<host/path> the repository that is the PROJECTS HOME — the
//                            GitHub recheck's corpus; free text, may be blank
//   projects_path=<path>     the ABSOLUTE folder whose subfolders are the
//                            projects (project_model.h owns the model)
//   last_project=<name>      the folder NAME opened last, written at every
//                            successful open; blank until the first
//   sync_path=<path>         the ABSOLUTE folder Synchronize to external
//                            storage mirrors this project into, or EMPTY for
//                            "not set up on this device" (external_sync.h)
//
// THAT IS THE WRITER'S ORDER and it is the architect's own (2026-08-30, given
// with the fifth key); the list above is this file's telling of it and
// kDeviceConfigKeys (device_config.cpp) is the one the program emits from.
//
// WHY IT EXISTS. `gui_scale` was a `.settings` key until 2026-08-27, which
// made it a fact about the PIECE: the same project opened on the laptop and
// on the tablet wants 100 and 225. Carrying it in the sidecar meant every
// sync of a project between the two devices had to rewrite it on the way over
// and put it back on the way home. It is the panel's business, so it
// follows the panel. (`audio_player`, the `l` command's external player, made
// the same move beside it and RETIRED WHOLE 2026-08-28 with the in-app render
// player — `l` now plays a render through the product's own playback engine
// on both devices, so there is no spawnable player to name; a config still
// carrying the key is unknown-key fatal, no migration, the architect's
// ruling.) THE OTHER THREE JOINED 2026-08-27 with the project model:
// where the projects live is a fact about the device (the clone's `projects/`
// on the laptop, the app's external files dir on the tablet); which repository
// is the projects home is ONE user's ONE repo, not a property of each piece,
// so `projects_repo` left the sidecar too (architect approval 2026-08-27, the
// fifth grant on settings_file.{h,cpp}); and what was opened last is what lets
// the tablet, which has no command line, open the right piece without a picker
// at startup. `sync_path` JOINED THEM 2026-08-30 for the same reason said in
// its bluntest form: where a machine's removable storage is mounted is a fact
// about the machine. The key REPLACED A DISCOVERY — the Synchronize act found
// the one mounted removable volume rather than being told (`/run/media/<user>/`
// on the laptop, `/storage/<name>` on the tablet) — and the finding rule worked
// on the laptop and COULD NOT WORK ON THE TABLET AT ALL, this One UI build
// mounting the OTG stick with `mountFlags=0` so that no `/storage/<uuid>` view
// exists for any app (measured 2026-08-28). A per-device destination is a
// per-device fact, which is what this file is for, and a configured path is
// what every desktop mirror does; the discovery is deleted whole, so the act
// has ONE road to its destination and no fallback chain (external_sync.h).
// The sidecar schema keeps everything that is about the music
// (settings_file.h, where the retired-key record lives).
//
// WHERE IT LIVES: `$XDG_CONFIG_HOME/warptempo_gui/config`, falling back to
// `$HOME/.config/warptempo_gui/config` — the same resolution shape the render
// cache uses for XDG_CACHE_HOME (render_cache.cpp), and ONE resolver with no
// `#ifdef` in it: the Android backend points XDG_CONFIG_HOME at the app's
// private internal directory before anything reads it (android_main,
// platform_android.cpp), exactly as it already does for the cache home.
//
// THE STRICTNESS POSTURE IS THE SIDECAR'S, DELIBERATELY. The file is
// program-written — the first run stamps it from the backend's own template and
// every later commit rewrites it — so any violation is a hand edit, which the
// two-category rule makes ADVERSARIAL: whole-file schema, EXACTLY the five keys
// and each of them exactly once, every key REQUIRED, one canonical spelling per
// value, and the FIRST error is fatal at startup with a blunt terminal line
// naming the path and the offending line. No repair, no partial apply, no
// silent fallback to defaults — a fallback would silently discard a value the
// user typed. The lexing itself is not respelled here: the shared scanner
// (warptempo_settings::scan_key_value_file, settings_file.h) owns "split at the
// first '=', no blank/comment/whitespace tolerance, no duplicate key, every
// required key present", and this file owns only the per-key grammar over it.
//
// ORDER IS THE WRITER'S, NOT THE READER'S — the sidecar's own posture again.
// The key list in device_config.cpp is the EMITTED order (gui_scale,
// projects_repo, projects_path, last_project, sync_path) and it is what
// every file this program writes carries; the shared scanner checks
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
// EVERY EDITABLE KEY HAS AN IN-APP ROAD SINCE 2026-09-02 (architect, the
// four-tier review's R-22): the Settings dropdown carries `GUI scale`,
// `Projects repository`, `Projects path` and `Sync path` as rows that open the
// settings editor prefilled, and the editor commits each through this file's
// writer under the key's own grammar below. Until that day the two path keys
// were hand-edited only, and HELP told the user to edit `sync_path` without
// telling him to quit first — which mattered, because A HAND EDIT UNDER A
// RUNNING APP IS CLOBBERED BY THE NEXT IN-APP COMMIT (R-6): the live struct is
// the truth and every commit rewrites the whole file from it. That stays so,
// and it is ordinary Linux behaviour for a program-written config; the in-app
// road is the sanctioned one now and the hand edit is the quit-first
// alternative. `last_project` is the one key with no editor — it is the
// program's own.

// The whole file, typed. The member defaults are CONSTRUCTION STATE, not load
// fallbacks: every key is required, so a successful read always assigns all
// five.
//
// TWO OF THEM MEAN SOMETHING BY BEING EMPTY, each saying so in its own grammar
// below: `last_project` empty is "nothing opened yet" and `sync_path` empty is
// "not set up on this device". (`projects_repo` also ADMITS empty, being free
// text, but empty is just a value there — one that never matches a remote.)
// The key is still REQUIRED in every case: the file admits no absent state,
// only an empty VALUE.
//
// The members are in the writer's order.
struct DeviceConfig {
    int         gui_scale = 100;
    std::string projects_repo;
    std::string projects_path;
    std::string last_project;
    std::string sync_path;
};

// The repository a device is STAMPED WITH when it has never named one — the
// first-run template's `projects_repo=` value on both backends. It moved here
// from settings_file.h with the key (architect approval 2026-08-27): the
// sidecar no longer references it, and AppState's own field reads it so a
// session that has not read the config yet names the same repository the
// template stamps. It is NOT a load fallback — the key is required, so a
// config either names a repository (blank included) or refuses.
inline constexpr const char* kDefaultProjectsRepo =
    "github.com/warptempo/warptempo_gui";

// THE gui_scale RANGE — the ONE owner, moved here from the `.settings` schema
// 2026-08-27 with the key (architect approval 2026-08-27). Both askers call it
// and neither respells the bracket: this file's reader, and the settings
// editor's `gui_scale=` red-flash ("loadable iff it commits", the standing
// rule).
//
// 100 is the design baseline (1920x1080, the supported laptop resolution); 200
// is the 4K case; 350 is the fine-panel ceiling — a 280 dpi tablet panel, where
// matching a coarser display's apparent size lands above 200 (225 % gives the
// 1024 logical width of the retired rig, ~305 % matches an 82 PPI external);
// 50 is the half-size floor, which is where every structural dimension in
// render.h's scaled_px accessors still has a floor holding it above zero.
//
// THE CEILING IS 350 (architect 2026-08-29, taking it down from the 400 that
// stood from 2026-08-26): 400 was never needed on either host, and 350 is
// where the EIGHT-LANE stack still fits a 1080-tall window — the top strip's
// 193 authored px plus the bottom row's 47 is 240, which at 350 % is 840, so
// the laptop's own screen keeps 240 px of waveform and gaps under the
// tallest scale the vocabulary admits. The tablet's 225 and its icon-row fit
// ceiling (240 since 2026-08-31 — the walk's own paragraph below) are
// untouched by the move.
//
// THE LAYOUT IS NOT WIDENED WITH THE CEILING, deliberately: below roughly
// 959 px of LOGICAL width (device width divided by the factor) the icon row's
// twenty-seven-button left-to-right walk runs past the window's right edge —
// the tablet's own 225 clears it with room to spare (2304/2.25 = 1024 logical
// px, 65 past the walk) and 240 is the fit ceiling on that panel since the
// centered lamp joined the row 2026-08-31 (249 at the 925-px walk before it;
// 250 was tried for
// an afternoon on 2026-08-27 and stepped back the same evening for the ~3
// authored px it shaved off the rightmost history icon) — and the redesign
// carries no collision rule anywhere — the crop-at-the-floor allowance recorded
// at kMinWindowWidthPx (render.h) is the standing answer. A scale is a
// VOCABULARY; which of its values lays out well is the architect's call on his
// own panel, not a validator's.
inline constexpr bool is_gui_scale_percent(int64_t v) {
    return v >= 50 && v <= 350;
}

// THE ASCII WHITESPACE SET this file's grammars refuse at a value's edges —
// all six of it, spelled as a byte set rather than asked of the locale, which
// no other grammar surface in the product consults either. ONE owner, TWO
// askers below: the shared path value grammar (which the file's two path keys
// are) and the project name's.
inline constexpr bool is_config_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}

// THE PATH VALUE GRAMMAR — the ONE owner for both path keys (`projects_path`
// below and `sync_path` under it): non-empty, ABSOLUTE, carrying NO LINE
// SEPARATOR anywhere (`\n` or `\r`) and NO ASCII WHITESPACE at either edge,
// and taken verbatim otherwise. A path may legitimately hold a space in the
// middle of a component, so only the edges are refused.
//
// THE TWO LEXICAL CLAUSES ARE `last_project`'s, for its reasons said of a path
// (the full argument is at is_last_project_name): the file is line-based
// `key=value` with NO ESCAPING, so a value carrying a line separator cannot be
// serialized at all, and an edge space is serializable but not canonical — and
// it would compose a DIFFERENT folder than the one the hand meant to name,
// silently, which is the mirror's own worst case. `\r` HAS A REAL PRODUCER:
// std::getline strips the `\n` of a CRLF line and leaves the `\r` on the
// value, so a config saved by a Windows editor refuses out loud here instead
// of aiming an act at `<path>\r`.
//
// EXISTENCE IS DELIBERATELY NOT A GRAMMAR QUESTION for either key: a
// `projects_path` that names no folder is startup's own "No project under
// <projects_path>" answer (project_model.h) and a `sync_path` that names no
// folder is the mirror's own destination refusal (external_sync.h) — the
// config is right about WHERE, and the thing may simply not be there yet.
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
// the config reader's `bad_value` line and the settings editor's red-flash
// card are the two readers of each, so a refusal says the same words on the
// terminal at startup and on a card at the commit. THE PATH REASONS NAME THE
// EDGE RULE because that is the fault a hand actually makes: an absolute path
// with a trailing space (or the `\r` a CRLF-saved file leaves on every value)
// is refused, and "must be an absolute path" alone would read as a lie
// against a value that plainly is one.
inline constexpr const char* kProjectsPathGrammarReason =
    "must be an absolute path with no whitespace at either edge";
inline constexpr const char* kSyncPathGrammarReason =
    "must be empty or an absolute path with no whitespace at either edge";
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
// producer the path keys' has (std::getline on a CRLF-saved file) and joined
// 2026-09-02 when the key gained this predicate — before it the reader took
// the value with no test at all, and the editor's own filter had always
// dropped control bytes, so the only value the clause newly refuses is a
// hand-edited one.
inline bool is_projects_repo(const std::string& v) {
    if (v.find('\n') != std::string::npos) return false;
    if (v.find('\r') != std::string::npos) return false;
    return true;
}

// THE sync_path GRAMMAR (architect 2026-08-30) — the path value grammar, OR
// EMPTY.
//
// EMPTY MEANS "NOT SET UP ON THIS DEVICE" and is the one absent state this
// file admits beside `last_project`'s: the Synchronize act then says
// `sync_path is not set` on a notification card and runs nothing
// (synchronize_to_external_storage, input_key_dispatch.cpp). It is the
// FIRST-RUN TEMPLATE'S value on both backends, because neither a laptop nor a
// tablet can be guessed at: where a stick gets mounted is the machine's
// answer, and a wrong guess would aim a mirror — its creates, its copies and
// its removals — at a folder the user never named.
//
// THE KEY IS SET IN-APP SINCE 2026-09-02 (architect, R-22): the Settings
// dropdown's `Sync path` row opens the settings editor on `sync_path=` and the
// commit writes this file under this very predicate — the template stamps it
// empty, the user types the path once (`/run/media/<user>/<stick>` on the
// laptop), and the act reads the LIVE struct, so the value is in force at the
// next `\` with no relaunch. A hand edit (with the app quit) is the other
// road. The writers' inventory is at write_device_config.
inline bool is_sync_path(const std::string& v) {
    return v.empty() || is_config_path_value(v);
}

// HOW A PATH UNDER THE PROJECTS PATH IS NAMED IN A SENTENCE — the basename
// rule's composer for everything the project model and the loaders say
// (messaging.md; `external_sync.cpp`'s `shown` is the mirror's own, relative
// to its two roots). A sentence that carries a path names THE PROJECT FOLDER
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
// successful open writes that name into this key (enumerate_project_names and
// the argument road, project_model.{h,cpp}, are the other two callers).
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
// hands to the atomic writer, and the first-run template's bytes too, so the
// stamped file and a committed one are one format by construction.
std::string format_device_config_text(const DeviceConfig& cfg);

// Parse and validate the whole config file at `path` under the schema at the
// head of this file. Errors carry a "line N: " prefix where a line is at fault;
// the caller adds the path context.
std::expected<DeviceConfig, std::string> read_device_config(
    const std::filesystem::path& path);

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
// The card stands, and it is a ruling rather than a leftover (architect
// 2026-09-04, blessing what landed 2026-09-02 under R-11's shape): the
// four-tier review had classed a failed persist as adversarial reach and asked
// for no message at all, but each of these writes is a deliberate press whose
// result nothing paints, and the deliberate-press rule (R-7's shape,
// messaging.md) is what decides such a case — so the failure says its
// sentence, on a NORMAL card, where the caller has a card surface.
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
// THREE CALL SITES CARRY THE FIVE KEY COMMITS, and this is their inventory
// (re-greped 2026-09-04, correcting a count this header had read as five):
// the scale's chokepoint GuiInputHandler::apply_gui_scale (input_handler.cpp);
// the settings editor's ONE device-key body, which serves three keys —
// `projects_repo=`, `projects_path=` and `sync_path=`
// (GuiSettingsEditor::commit_device_setting, settings_editor.cpp; the two path
// arms joined 2026-09-02 under R-22, and the `audio_player=` arm retired with
// its key 2026-08-28); and gui_main's `last_project` write on the success path
// of every open (main.cpp). A same-value commit never reaches any of them —
// each gates the no-op ahead of the write — so a file rewrite means a value
// actually moved. `last_project` is the one key with no editor: it is the
// program's own.
std::optional<GuiFailure> write_device_config(const DeviceConfig& cfg);

// STARTUP: the config, created from `first_run_template` if the file does not
// exist yet and then read back like any other. The template is the running
// BACKEND's answer (GuiPlatform::device_config_defaults — a platform fact, not
// a GUI one: the laptop wants 100 % and the clone's `projects/`, the tablet
// 225 % and its external files dir's `projects/`;
// both stamp kDefaultProjectsRepo, a blank sync_path and a blank
// last_project), so a first run on
// either device lands a file that is already right for it and the user edits
// from there rather than from a wrong guess. A missing parent directory is
// created.
//
// The error is the whole failure vocabulary of this file: an unresolvable
// config home, a create that failed, an unopenable file, or the reader's first
// schema refusal. Every one of them is FATAL at the caller (gui_main) — the
// blunt terminal line and no window.
std::expected<DeviceConfig, std::string> load_device_config(
    const DeviceConfig& first_run_template);
