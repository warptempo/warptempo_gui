#pragma once

#include "render.h"   // GuiPalette, GuiColor: the role table's members

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// THE THEME FILES (architect 2026-10-04) — every colour the GUI paints, as
// ROLES, and where a theme other than the one built-in comes from.
//
// A THEME IS A VALUE FOR EVERY ROLE. The roles are the role table below —
// THE ONE ENUMERATION: its name (what a theme file spells), the GuiPalette
// field it fills (render.h, whose palette block owns what each role paints)
// and THE BUILT-IN's value. The chrome's roles are Windows 2000 Standard's
// recorded chrome (architect 2026-10-06, the Windows 2000 pivot; the catalog
// entry `windows-2000-standard`'s bytes, its setup hive's Control
// Panel\Colors: ButtonFace, ButtonText, the relief quartet, Hilight /
// HilightText, Window / WindowText; Windows' status bar's ButtonFace /
// ButtonText for the clock panel; Windows' InfoWindow / InfoText and its
// tooltip's black border for the cards; ActiveTitle / GradientActiveTitle /
// TitleText and InactiveTitle / GradientInactiveTitle / InactiveTitleText for
// the caption), and the program's roles are the planner's picks from Windows'
// twenty solid colours (the architect's delegation, 2026-10-04: "the default
// theme is just a fallback"), each flag kind a face and a selected face of the
// VGA sixteen under white labels (the role table's program block).
//
// ONE THEME IS BUILT IN, compiled: `windows-2000-standard` (kBuiltinThemeKey),
// the role table's values — Windows 2000's own "Windows Standard" scheme, the
// chrome's (chrome_spec.h) — AND THE DEVICE CONFIG'S DEFAULT `theme`
// (kDefaultThemeKey). Every other theme is a FILE, Windows 95's recorded
// colours (`windows-95-standard`) among them since 2026-10-06.
//
// THE FILES: `<key>.theme` in the `themes/` folder BESIDE THE DEVICE CONFIG
// (theme_folder_path: device_config_path()'s own folder, on both devices — on
// the tablet the app's private internal directory, through the same
// XDG_CONFIG_HOME the backend sets before gui_main runs). THE FOLDER IS READ
// ONCE, AT LAUNCH (read_theme_folder, gui_main, ahead of the device config so
// its `theme` key can be judged against what loaded): every regular file
// whose name ends `.theme` is parsed then and held for the process's life,
// and nothing re-reads the folder later — a file added or edited under a
// running app takes effect at the next launch. A MISSING FOLDER IS NO FILES
// (the built-in alone). A name not ending `.theme` is not a theme and is not
// read; neither is a directory or any other non-regular entry.
//
// THE BUNDLED FILES ARE COPIED IN AT EVERY LAUNCH (architect 2026-10-05):
// every theme other than the built-in — the imported catalog, `warptempo`
// and his colour-picker presets — SHIPS WITH THE
// PROGRAM as a `.theme` file (generated into the repository's
// `assets/themes/` by tools/theme_catalog/gen_theme_files.py, which states
// what each file names), and each launch, BEFORE THE ONE READ, writes every
// bundled file into the themes folder (copy_in_bundled_themes, gui_main),
// creating the folder, overwriting a file of the same name: THE BUNDLE WINS
// FOR ITS OWN NAMES, so a bundled name edited by hand is overwritten at the
// next launch, and HIS OWN FILES TAKE OTHER NAMES — a file of any other name
// is never touched — SAVE ONE: a file bearing the BUILT-IN'S name is deleted
// by the copy-in with one advisory line, the app's own former copy left by a
// rename of the built-in (the reason at copy_in_bundled_themes,
// theme_file.cpp). Then the folder is read once as above, so a bundled file
// is read exactly like his own. THE SOURCE IS PER DEVICE, behind the seam
// (GuiPlatform::bundled_theme_files): on the laptop the repository's
// `assets/themes/`, its absolute path compiled in (the laptop runs from its
// build tree); on the tablet the APK's `themes/` assets, which build_apk.sh
// packs. A copy-in that cannot read the bundle or write a file is the
// launch's hard fail on the read's own road (below).
//
// THE GRAMMAR (TEXT IS ASCII IN GRAMMARS): LF-terminated `role=value` lines
// under the device config's own lexical contract (the shared scanner,
// warptempo_settings::scan_key_value_file: split at the first '=', no blank
// line, no comment, no whitespace tolerance, no duplicate), each role a name
// of the role table and each value THE ONE COLOUR GRAMMAR (theme_colour_word,
// below). A FILE MAY NAME ONLY SOME ROLES, in any order: every role it does
// not name takes the built-in's value — WITH ONE RULE OF THE FILE'S OWN, THE
// FLAT CAPTION (architect 2026-10-05): a file that names a caption's START
// (`caption_active` / `caption_inactive`) and not its GRADIENT END
// (`caption_active_gradient` / `caption_inactive_gradient`) gets the end
// EQUAL TO THE START — a flat caption, as a theme recording no gradient
// (Windows 95's, Plus!'s, KDE 3's, CDE's) drew one — never the built-in's end
// under the file's start, which would be a gradient no one recorded. The
// pairs are kGuiThemeCaptionGradients below and the rule is applied once, at
// the file's read (read_theme_file). The stem is the theme's KEY, under the
// key grammar (is_theme_key_spelling).
//
// EVERY VIOLATION IS THE LAUNCH'S FIRST-ERROR HARD FAIL (NO BACKSTOPS FOR
// ADVERSARIAL USE: the files are the user's own, so a violation is a hand
// edit): an unknown role, a malformed value, a duplicate role, a stem outside
// the key grammar, a file named for the built-in, an unreadable folder or
// file — the blunt terminal line naming the file (and the line, where a line
// is at fault) and no window, the road a malformed device config takes on
// each platform (gui_main's stderr line, which the tablet's backend carries
// to logcat, then the exit). No recovery and no partial load: the folder is
// taken whole or the program does not start. The files are taken in name
// order, so the first error is the same one on every launch.

// The built-in's key — the one theme compiled into the program (architect
// 2026-10-06): Windows 2000's scheme (tools/theme_catalog's
// `windows-2000-standard`), the chrome's colours.
inline constexpr const char* kBuiltinThemeKey = "windows-2000-standard";
// THE DEVICE CONFIG'S DEFAULT `theme` (architect 2026-10-06): the built-in —
// what a first-run config names (DeviceConfig::theme, whose initializer
// spells it; theme_file.cpp's static_assert keeps the two in step). Compiled
// in, so the default always resolves.
inline constexpr const char* kDefaultThemeKey = kBuiltinThemeKey;

// THE ROLE TABLE — THE ONE ENUMERATION (architect 2026-10-04), in GuiPalette's
// order: the chrome's twenty-one (the caption's six since 2026-10-05), then the
// program's fifteen. The reader's arm,
// the built-in, the files' fill and install_palette (render.cpp) all walk it,
// so a role cannot be read and not painted. What each role paints is
// render.h's palette block (THE MAPPING).
struct GuiThemeRole {
    const char*          name;
    GuiColor GuiPalette::* member;
    uint32_t             builtin;   // 0xRRGGBB, the built-in theme's value
};
inline constexpr GuiThemeRole kGuiThemeRoles[] = {
    // THE CHROME — Windows 2000 Standard as recorded.
    {"ground",                    &GuiPalette::ground,                    0xD4D0C8},
    {"label",                     &GuiPalette::label,                     0x000000},
    {"hilight",                   &GuiPalette::hilight,                   0xFFFFFF},
    {"light_3d",                  &GuiPalette::light_3d,                  0xD4D0C8},
    {"shadow",                    &GuiPalette::shadow,                    0x808080},
    {"dk_shadow",                 &GuiPalette::dk_shadow,                 0x404040},
    {"selected_fill",             &GuiPalette::selected_fill,             0x0A246A},
    {"selected_text",             &GuiPalette::selected_text,             0xFFFFFF},
    {"field_ground",              &GuiPalette::field_ground,              0xFFFFFF},
    {"field_text",                &GuiPalette::field_text,                0x000000},
    {"clock_ground",              &GuiPalette::clock_ground,              0xD4D0C8},
    {"clock_text",                &GuiPalette::clock_text,                0x000000},
    {"card_ground",               &GuiPalette::card_ground,               0xFFFFE1},
    {"card_text",                 &GuiPalette::card_text,                 0x000000},
    {"card_frame",                &GuiPalette::card_frame,                0x000000},
    // THE CAPTION (architect 2026-10-05): the window's title bar, ACTIVE
    // while the window has the focus and INACTIVE without it (the tablet's is
    // always active, GuiPlatform::caption_active), each a START at the left,
    // a GRADIENT END at the right and the title's TEXT — Windows' ActiveTitle
    // / GradientActiveTitle / TitleText and InactiveTitle /
    // GradientInactiveTitle / InactiveTitleText. The built-in is Windows
    // 2000's navy-to-sky-blue caption under white and its grey-to-silver one
    // under the face's colour.
    {"caption_active",            &GuiPalette::caption_active,            0x0A246A},
    {"caption_active_gradient",   &GuiPalette::caption_active_gradient,   0xA6CAF0},
    {"caption_active_text",       &GuiPalette::caption_active_text,       0xFFFFFF},
    {"caption_inactive",          &GuiPalette::caption_inactive,          0x808080},
    {"caption_inactive_gradient", &GuiPalette::caption_inactive_gradient, 0xC0C0C0},
    {"caption_inactive_text",     &GuiPalette::caption_inactive_text,     0xD4D0C8},
    // THE PROGRAM'S OWN ELEMENTS — Windows' twenty solid colours: the canvas
    // black under Sound Recorder's lime trace (measured on his Windows 98
    // screenshot 2026-10-05), the lit outline green; each flag kind a dark
    // face and a selected face, each kind a true dark / bright pair of
    // Windows' sixteen (architect 2026-10-05) — warp purple / fuchsia, phase
    // reset teal / aqua, the history's added green / lime, its removed
    // maroon / red, which the invalid flag wears too; BOTH FLAG LABELS WHITE
    // (architect 2026-10-05: "white text for the flags is going to be the
    // most common; the highlights will generally be chosen so white shows"
    // — so a file that names flag faces and no labels gets white on both);
    // the playhead's stem white, the scanner yellow (architect 2026-10-05,
    // undoing that day's earlier yellow stem: the scanner is its own role
    // now, Windows' own "yellow", one of the twenty named colours — the head
    // is no longer a role at all, WordPad's ruler marker painted in the
    // chrome's own label / hilight / shadow / ground, below).
    {"waveform_canvas",           &GuiPalette::waveform_canvas,           0x000000},
    {"waveform_ink",              &GuiPalette::waveform_ink,              0x00FF00},
    {"waveform_outline",          &GuiPalette::waveform_outline,          0x008000},
    {"warp_flag",                 &GuiPalette::warp_flag,                 0x800080},
    {"warp_flag_selected",        &GuiPalette::warp_flag_selected,        0xFF00FF},
    {"phase_reset_flag",          &GuiPalette::phase_reset_flag,          0x008080},
    {"phase_reset_flag_selected", &GuiPalette::phase_reset_flag_selected, 0x00FFFF},
    {"added_flag",                &GuiPalette::added_flag,                0x008000},
    {"added_flag_selected",       &GuiPalette::added_flag_selected,       0x00FF00},
    {"removed_flag",              &GuiPalette::removed_flag,              0x800000},
    {"removed_flag_selected",     &GuiPalette::removed_flag_selected,     0xFF0000},
    {"flag_label",                &GuiPalette::flag_label,                0xFFFFFF},
    {"flag_label_selected",       &GuiPalette::flag_label_selected,       0xFFFFFF},
    {"playhead_stem",             &GuiPalette::playhead_stem,             0xFFFFFF},
    {"scanner",                   &GuiPalette::scanner,                   0xFFFF00},
};
inline constexpr std::size_t kGuiThemeRoleCount = std::size(kGuiThemeRoles);

// ONE THEME'S VALUES, as words in the role table's order — what a file
// resolves to (the built-in's words, overwritten by the roles it names) and
// what install_palette reads.
using GuiThemeWords = std::array<uint32_t, kGuiThemeRoleCount>;

// The index of the role named `name` in the table, or kGuiThemeRoleCount —
// the reader's one lookup, and install_palette's for the plate's two baked
// inks (waveform_plate_inks).
constexpr std::size_t theme_role_index(std::string_view name) {
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        if (name == kGuiThemeRoles[i].name) return i;
    return kGuiThemeRoleCount;
}

// THE FLAT CAPTION'S PAIRS — each caption's start and its gradient end, the
// grammar's one rule of its own (the head): a file naming the first and not
// the second gets the second equal to the first.
struct GuiThemeGradientPair {
    std::size_t start;
    std::size_t end;
};
inline constexpr GuiThemeGradientPair kGuiThemeCaptionGradients[] = {
    {theme_role_index("caption_active"),
     theme_role_index("caption_active_gradient")},
    {theme_role_index("caption_inactive"),
     theme_role_index("caption_inactive_gradient")},
};

// THE ONE COLOUR GRAMMAR (architect 2026-10-03; every role's since
// 2026-10-04): `#` and six hexadecimal digits, either case, OR one of
// WINDOWS' TWENTY ALWAYS-SOLID COLOURS by name, lowercase — the colours a
// 256-colour display's system palette reserved, so every program of the era
// could count on them drawing solid:
//   THE VGA SIXTEEN under their HTML 4.01 names (the W3C HTML 4.01
//   Recommendation, 1999, section 6.5 "Colors"): black #000000, maroon
//   #800000, green #008000, olive #808000, navy #000080, purple #800080, teal
//   #008080, silver #C0C0C0, gray #808080, red #FF0000, lime #00FF00, yellow
//   #FFFF00, blue #0000FF, fuchsia #FF00FF, aqua #00FFFF, white #FFFFFF;
//   THE FOUR WINDOWS RESERVES under Delphi VCL's names (Graphics.pas's
//   clMoneyGreen, clSkyBlue, clCream, clMedGray, without the `cl`):
//   moneygreen #C0DCC0, skyblue #A6CAF0, cream #FFFBF0, medgray #A0A0A4.
// The word is the value's 0xRRGGBB; nothing else is a colour.
struct NamedThemeColour {
    const char* name;
    uint32_t    rgb;
};
inline constexpr NamedThemeColour kNamedThemeColours[] = {
    {"black", 0x000000},  {"maroon", 0x800000}, {"green", 0x008000},
    {"olive", 0x808000},  {"navy", 0x000080},   {"purple", 0x800080},
    {"teal", 0x008080},   {"silver", 0xC0C0C0}, {"gray", 0x808080},
    {"red", 0xFF0000},    {"lime", 0x00FF00},   {"yellow", 0xFFFF00},
    {"blue", 0x0000FF},   {"fuchsia", 0xFF00FF}, {"aqua", 0x00FFFF},
    {"white", 0xFFFFFF},
    {"moneygreen", 0xC0DCC0}, {"skyblue", 0xA6CAF0}, {"cream", 0xFFFBF0},
    {"medgray", 0xA0A0A4},
};
inline std::optional<uint32_t> theme_colour_word(std::string_view v) {
    for (const NamedThemeColour& n : kNamedThemeColours)
        if (v == n.name) return n.rgb;
    if (v.size() != 7 || v[0] != '#') return std::nullopt;
    uint32_t w = 0;
    for (size_t i = 1; i < 7; ++i) {
        const char c = v[i];
        uint32_t d = 0;
        if (c >= '0' && c <= '9')      d = static_cast<uint32_t>(c - '0');
        else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a' + 10);
        else return std::nullopt;
        w = (w << 4) | d;
    }
    return w;
}

// THE THEME-KEY GRAMMAR — the spelling of a key, which is a theme file's stem
// (architect 2026-10-04, re-derived from the catalog's hundred keys of that
// day, every one of which it admits): one or more runs of lowercase ASCII
// letters and digits joined by single hyphens — no leading, trailing or
// doubled hyphen. A file whose stem breaks it is the launch's hard fail.
constexpr bool is_theme_key_spelling(std::string_view v) {
    if (v.empty() || v.front() == '-' || v.back() == '-') return false;
    char prev = 0;
    for (const char c : v) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!alnum && c != '-') return false;
        if (c == '-' && prev == '-') return false;
        prev = c;
    }
    return true;
}

// The themes folder: `themes/` in device_config_path()'s folder, or an EMPTY
// path when the config home does not resolve (the config's own load then
// refuses with its own line).
std::filesystem::path theme_folder_path();

// THE LAUNCH'S COPY-IN (gui_main, first, before read_theme_folder): every
// bundled `.theme` file written into the themes folder under the rule above,
// the folder created, a file already holding the bundle's bytes left as it
// is, and a file of the built-in's name deleted first. Answers nothing on
// success (or when the config home does not resolve: there is no folder, and
// the config's load refuses with its own line) and the failure's whole line
// otherwise — FATAL at the caller.
std::optional<std::string> copy_in_bundled_themes();

// THE LAUNCH'S ONE READ of the themes folder (gui_main, before the device
// config is read): every `*.theme` regular file parsed under the grammar
// above and held. Answers nothing on success and the first error's whole
// line otherwise, the file named — FATAL at the caller. Called once per
// process; nothing else reads the folder.
std::optional<std::string> read_theme_folder();

// THE `theme` KEY'S GRAMMAR — the ONE owner, asked by the device config's
// reader (a hand-edited key naming no theme is the launch's hard fail) and by
// the settings editor's Theme row (its refused commit): the built-in's key or
// the key of a file read at launch, byte for byte. No nearest match and no
// fallback theme.
bool is_theme_key(const std::string& v);
inline constexpr const char* kThemeGrammarReason =
    "must be windows-2000-standard or a theme file's key read at launch";

// The words of the theme `key` names — the built-in's or a loaded file's.
// Precondition: is_theme_key(key), every caller's value having come through
// that grammar, so this resolves and never refuses (install_palette).
const GuiThemeWords& theme_words(std::string_view key);
