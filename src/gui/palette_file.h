#pragma once

#include "render.h"   // GuiPalette, GuiColor: the role table's members

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// THE PALETTE FILES (architect 2026-10-07) — the PROGRAM'S colors, a file
// type of their own beside the chrome's theme files (theme_file.h).
//
// TWO FILE TYPES, TWO SOURCES FOR ONE STRUCT (architect 2026-10-07): the
// CHROME THEME (the `theme` device key: the built-in and the bundled catalog
// files, never edited in the app) colors the chrome, and THE PALETTE colors
// the program, picked in the app and saved as NAMED presets. GuiPalette
// (render.h) keeps every member; install_palette (render.h) fills the chrome
// members off the theme's role table (kGuiThemeRoles) and the program members
// off this file's (kGuiPaletteRoles), so every painter reads palette() as
// before and no painter knows which file a color came from.
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// playhead's head, WordPad's ruler marker in the chrome's roles, and the flag
// editor's selection band, the chrome's selected pair). So the palette is
// exactly the FOURTEEN roles below: the waveform's canvas, ink and lit
// outline, the four flag kinds' faces and selected faces, the one flag label,
// the playhead's stem and the scanner. THE FLAG OUTLINE IS NO ROLE: it is
// always the canvas (architect 2026-10-07: "otherwise it will poke out when
// going into the canvas"; render.h's marker-lane paragraph), and THE FLAG
// LABEL IS ONE for every face, resting and selected (his one-label rule of
// the morning; the four per-kind selected labels retired the same day).
//
// THE TWO DEFAULT PALETTES ARE COMPILED IN, one per chrome vocabulary, each
// named by its ChromeSpec's `default_palette` (chrome_spec.h): `windows-2000`
// and `clearlooks` — the role table's two value columns below. A DEFAULT IS
// NOT A FILE AND TAKES NO FILE (the built-in theme's rule): a file bearing a
// default's name is the read's hard fail, and the maintenance API below
// refuses to write, rename or remove one. Nothing is bundled or copied in for
// palettes; the folder holds his own files alone.
//
// THE FILES: `<name>.palette` in the `palettes/` folder BESIDE THE DEVICE
// CONFIG (palette_folder_path, as theme_folder_path is). THE GRAMMAR is the
// theme file's lexical contract exactly (the shared scanner,
// warptempo_settings::scan_key_value_file: LF-terminated `role=value` lines,
// split at the first '=', no blank line, no comment, no whitespace
// tolerance, no duplicate), each role one of the fourteen and each value THE
// ONE COLOR GRAMMAR (theme_colour_word, theme_file.h). A FILE MAY NAME ONLY
// SOME ROLES: a role it does not name takes THE LIVE CHROME'S DEFAULT
// PALETTE'S value, resolved when the words are asked for (palette_words),
// never at the read — the read precedes the chrome's choice — and never
// `windows-2000`'s under clearlooks, which would mix two vocabularies' grays
// in one well. No follower and no caption rule: every role stands alone.
// The picker writes all fourteen, so only a hand-written file names some.
//
// THE NAME GRAMMAR (is_palette_name_spelling): the stem is the name
// verbatim, case-sensitive, 1 to 40 bytes of printable ASCII (0x20..0x7E),
// no leading or trailing space and no '/'. WIDER THAN THE THEME KEY'S
// hyphen grammar on purpose: a theme key is a catalog's machine key, a
// palette name is TYPED by him through the on-screen keyboard "for
// organization", so it admits capitals, spaces and punctuation; ASCII
// still, TEXT BEING ASCII IN GRAMMARS. The '/' is the one byte a file name
// cannot hold; the suffix is always appended, so no name reaches outside
// the folder.
//
// THE FOLDER IS READ ONCE AT LAUNCH (read_palette_folder, gui_main, after
// read_theme_folder and before the device config, so the config's `palette`
// key is judged against what loaded; the names taken in sorted order, so
// the first error is the same file on every launch) into a live map. A
// MISSING FOLDER IS NO FILES. EVERY VIOLATION IS THE LAUNCH'S FIRST-ERROR
// HARD FAIL (NO BACKSTOPS FOR ADVERSARIAL USE: a violation is a hand edit) —
// an unknown role (a chrome role named in a palette file included), a
// malformed value, a duplicate role, a name outside the grammar, a file named
// for a default, an unreadable folder or file — the blunt terminal line
// naming the file and the line and no window, the theme read's road and
// words.
//
// UNLIKE THE THEMES, THE MAP IS MAINTAINED AFTER THE LAUNCH BY THE PICKER'S
// OWN WRITES (the picker is the one writer of the folder): write, rename and
// remove below each keep the map and the folder in step, so the folder is
// never re-read. THE CALLERS (2026-10-07): the launch read, and THE COLOR
// PICKER'S PRESET ACTS (color_picker.h's THE PRESETS: Save and Save As
// write, Rename renames, Delete removes — GuiColorPicker::save_palette,
// commit_name, confirm_delete), its picks themselves writing the LIVE
// WORDS alone (install_program_palette) and no file. None of the writers
// below touches the device config — the `palette` key is the caller's,
// which owes its own rewrite when it renames or removes the palette the
// key names (GuiColorPicker::write_palette_key). Each answers nothing on
// success and the failure's whole line otherwise, which the caller cards
// (the validation doctrine's class 5).

// THE ROLE TABLE — THE ONE ENUMERATION of the program's roles (architect
// 2026-10-07), in GuiPalette's order: the name a palette file spells, the
// GuiPalette member it fills (render.h's palette block owns what each
// paints) and THE TWO DEFAULT PALETTES' values.
struct GuiPaletteRole {
    const char*            name;
    GuiColor GuiPalette::* member;
    uint32_t               windows_2000;   // 0xRRGGBB, the `windows-2000` default
    uint32_t               clearlooks;     // 0xRRGGBB, the `clearlooks` default
};
// THE `windows-2000` COLUMN is Windows' own: THE WAVEFORM in Windows' twenty
// solid colors, the canvas black under Sound Recorder's lime trace (measured
// on his Windows 98 screenshot 2026-10-05), the lit outline green. THE FLAG
// PAIRS ARE WINDOWS 2000'S SCHEME BYTES (architect 2026-10-07: "coherence
// with the theme — Windows 2000 is the default"; "how well the selected
// version shows up on the flag"), each authoring kind a face and its brighter
// accent as Microsoft's own schemes paired them — WARP the Standard scheme's
// ActiveTitle 0A246A selected to its Background 3A6EA5; PHASE RESET the Teal
// scheme's ActiveTitle 008080 selected to its GradientActiveTitle 00CCD8 —
// and the history's two Windows' true dark / bright pairs of the sixteen,
// ADDED green 008000 / lime 00FF00 and REMOVED maroon 800000 / red FF0000,
// which the invalid flag wears too (render.h's marker-lane paragraph). THE
// ONE FLAG LABEL WHITE on every face, resting and selected — so white stands
// on the bright accents 00CCD8, 00FF00 and FF0000 too, at WCAG contrasts of
// 2.0, 1.4 and 4.0 to 1 where the retired per-kind black labels gave 10.6,
// 15.3 and 5.3 (his one-label rule, 2026-10-07: the label is one color; the
// faint white on the cyan and the lime is the rule's accepted price, said
// plainly). THE PLAYHEAD'S STEM AND THE SCANNER
// BOTH WHITE (architect 2026-10-07: "let's go back to a white scanner") and
// still TWO roles, so a palette may part them — the head is no role at all,
// WordPad's ruler marker painted in the chrome's own roles (render.h).
//
// THE `clearlooks` COLUMN is his picks on the CL10 sheets (2026-10-07 ~05:30;
// tmp/clearlooks/report_CL10.md), each byte the engine's own arithmetic on
// squeeze's gtkrc colors (ge_shade_color through cairo, as
// tools/theme_catalog/build.py's tones are): the canvas BLACK, his pick (CL10a
// band 3: the canvas under the accent ink); the ink bg[SELECTED] 86ABD9 at
// the engine's 1.3 stop, D2E3F7, the selected flag's byte (architect
// 2026-10-07 ~09:00: spot[1] read too dark on the black canvas); the lit
// outline bg[SELECTED] itself (CL10a band 3); ONE PAIR FOR WARP AND PHASE
// RESET (a scene shows one column, report CL10 section 4): bg[SELECTED] at
// rest, its 1.3 stop selected (CL10a); the history's pair sheet d's F1, THE
// GNOME HIG's palette green 83A67F and red C1665A at rest, each lit by the
// engine's 1.3 when selected (B3CEB0, E2988E), the invalid flag wearing the
// red pair; the flag label fg_color, BLACK on every face; the playhead's stem
// and the scanner WHITE, his ruling (CL10a). Under clearlooks the flag
// outline was the canvas already, so the one outline rule changes nothing
// there.
inline constexpr GuiPaletteRole kGuiPaletteRoles[] = {
    {"waveform_canvas",           &GuiPalette::waveform_canvas,           0x000000, 0x000000},
    {"waveform_ink",              &GuiPalette::waveform_ink,              0x00FF00, 0xD2E3F7},
    {"waveform_outline",          &GuiPalette::waveform_outline,          0x008000, 0x86ABD9},
    {"warp_flag",                 &GuiPalette::warp_flag,                 0x0A246A, 0x86ABD9},
    {"warp_flag_selected",        &GuiPalette::warp_flag_selected,        0x3A6EA5, 0xD2E3F7},
    {"phase_reset_flag",          &GuiPalette::phase_reset_flag,          0x008080, 0x86ABD9},
    {"phase_reset_flag_selected", &GuiPalette::phase_reset_flag_selected, 0x00CCD8, 0xD2E3F7},
    {"added_flag",                &GuiPalette::added_flag,                0x008000, 0x83A67F},
    {"added_flag_selected",       &GuiPalette::added_flag_selected,       0x00FF00, 0xB3CEB0},
    {"removed_flag",              &GuiPalette::removed_flag,              0x800000, 0xC1665A},
    {"removed_flag_selected",     &GuiPalette::removed_flag_selected,     0xFF0000, 0xE2988E},
    {"flag_label",                &GuiPalette::flag_label,                0xFFFFFF, 0x000000},
    {"playhead_stem",             &GuiPalette::playhead_stem,             0xFFFFFF, 0xFFFFFF},
    {"scanner",                   &GuiPalette::scanner,                   0xFFFFFF, 0xFFFFFF},
};
inline constexpr std::size_t kGuiPaletteRoleCount = std::size(kGuiPaletteRoles);

// ONE PALETTE'S VALUES, as words in the role table's order — what
// palette_words answers, what install_program_palette (render.h) takes and
// what write_palette_file writes.
using GuiPaletteWords = std::array<uint32_t, kGuiPaletteRoleCount>;

// The index of the role named `name` in the table, or kGuiPaletteRoleCount —
// the reader's one lookup, and install_palette's for the plate's two baked
// inks (waveform_plate_inks).
constexpr std::size_t palette_role_index(std::string_view name) {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        if (name == kGuiPaletteRoles[i].name) return i;
    return kGuiPaletteRoleCount;
}

// INSTALL THE PROGRAM'S FOURTEEN ALONE (2026-10-07; the install family's
// second member, defined in render.cpp beside install_palette, render.h):
// `words` written into GuiPalette's program members, the plate's two inks
// re-baked and palette_generation bumped, the chrome members untouched — THE
// CHROME STAYS LAUNCH-BOUND (its theme moves only with install_palette and
// the `theme` key), the program's colors move live. THE PICKER'S LIVE ROAD
// (GuiColorPicker::set_color, color_picker.cpp). THE APPLY SHAPE THE CALLER OWES, after
// the call, is the settings editor's theme arm's: the synchronous plate
// rebuild (Viewport::kick_waveform_sync — the plate re-rendered in the new
// inks, the flag cache rebuilt at its tail, keyed by the generation) and the
// whole window damaged (Viewport::invalidate_all), so the swap is one frame.
// WHY THAT IS ENOUGH (re-grepped 2026-10-07): the fourteen bake into two
// cached things alone — the waveform plate, keyed by its two inks
// (waveform_plate_inks, the worker's job carrying them), and the flag cache,
// keyed by the generation (FlagCache::fp_palette_generation), whose rebuild
// also stages the stem stash and its colors (AppState::staged_marker_stems,
// swapped in at the publish); every other reader paints live from palette()
// each frame — the canvas (render_canvas), the stems' flanks
// (paint_marker_stem_flanks), the open flag editor (render_flag_editor_box),
// the lead-in ring (phase_reset_stem_color), the playhead's stem, the zoom
// anchor's and the scanner.
void install_program_palette(const GuiPaletteWords& words);

// THE DEFAULT PALETTES — each a name and its column of the role table, in
// the vocabularies' order (kGuiChromeSpecs; palette_file.cpp asserts that
// every ChromeSpec's default_palette names one and that the two orders
// agree).
struct GuiDefaultPalette {
    const char*              name;
    uint32_t GuiPaletteRole::* column;
};
inline constexpr GuiDefaultPalette kGuiDefaultPalettes[] = {
    {"windows-2000", &GuiPaletteRole::windows_2000},
    {"clearlooks",   &GuiPaletteRole::clearlooks},
};

// THE NAME GRAMMAR (the head): 1 to 40 bytes of printable ASCII, no leading
// or trailing space, no '/'. A file whose stem breaks it is the launch's
// hard fail; a name the picker would save under it is the writer's refusal.
inline constexpr std::size_t kPaletteNameMaxBytes = 40;
constexpr bool is_palette_name_spelling(std::string_view v) {
    if (v.empty() || v.size() > kPaletteNameMaxBytes) return false;
    if (v.front() == ' ' || v.back() == ' ') return false;
    for (const char c : v)
        if (c < 0x20 || c > 0x7E || c == '/') return false;
    return true;
}

// Whether `name` is a default palette's (kGuiDefaultPalettes), byte for byte.
constexpr bool is_default_palette_name(std::string_view name) {
    for (const GuiDefaultPalette& d : kGuiDefaultPalettes)
        if (name == d.name) return true;
    return false;
}

// THE PALETTE THE INSTALL TAKES (architect 2026-10-07): the device config's
// `palette` as written, or — EMPTY, no `palette` line — THE LIVE CHROME'S
// DEFAULT PALETTE (chrome_spec.h's default_palette of live_chrome_spec():
// `windows-2000` or `clearlooks`), the theme's own shape
// (effective_theme_key). The struct keeps the empty value, so the file
// never pins a vocabulary's default.
std::string_view effective_palette_name(std::string_view palette);

// The palettes folder: `palettes/` in device_config_path()'s folder, or an
// EMPTY path when the config home does not resolve (the config's own load
// then refuses with its own line).
std::filesystem::path palette_folder_path();

// THE LAUNCH'S ONE READ of the palettes folder (gui_main, after
// read_theme_folder, before the device config is read): every `*.palette`
// regular file parsed under the grammar above into the live map. Answers
// nothing on success and the first error's whole line otherwise, the file
// named — FATAL at the caller. Called once per process.
std::optional<std::string> read_palette_folder();

// EVERY PALETTE'S NAME, for the picker's list: the defaults first, in the
// vocabularies' order, then the loaded files in byte order of their names.
std::vector<std::string> palette_names();

// THE `palette` KEY'S GRAMMAR — the ONE owner, asked by the device config's
// reader (a hand-edited key naming no palette is the launch's hard fail): a
// default's name or a loaded file's, byte for byte.
bool is_palette_name(std::string_view name);
inline constexpr const char* kPaletteGrammarReason =
    "must be windows-2000, clearlooks or the name of a palette file read at "
    "launch";

// The words of the palette `name` names — a default's column, or a loaded
// file's roles over THE LIVE CHROME'S DEFAULT for every role the file does
// not name (the head). Precondition: is_palette_name(name), and the live
// chrome already chosen (set_live_chrome_spec), so this resolves and never
// refuses (install_palette; the picker).
GuiPaletteWords palette_words(std::string_view name);

// THE PICKER'S WRITES (the head: each keeps the map and the folder in step,
// none touches the device config, each answers the failure's whole line).
//
// write_palette_file: create or overwrite `<name>.palette` with all fourteen
// roles, uppercase `#RRGGBB` in the table's order, through the atomic writer
// (atomic_write_string_to_path), the folder created on the first write.
// Refuses a name outside the grammar and a default's name.
std::optional<std::string> write_palette_file(std::string_view name,
                                              const GuiPaletteWords& words);
// rename_palette_file: `old_name`, a loaded file's (the picker lists only
// what exists: a miss is a program bug, asserted), becomes `new_name`.
// Refuses a default's name on either side, a new name outside the grammar
// and a new name already taken; the same name twice is a no-op.
std::optional<std::string> rename_palette_file(std::string_view old_name,
                                               std::string_view new_name);
// remove_palette_file: delete a loaded file's `<name>.palette` (a miss is a
// program bug, asserted). Refuses a default's name.
std::optional<std::string> remove_palette_file(std::string_view name);
