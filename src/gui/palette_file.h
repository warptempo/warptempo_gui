#pragma once

#include "render.h"   // GuiPalette, GuiColor: the role table's members

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// THE PRESET FILES (architect 2026-10-07; split in two 2026-10-08 ~18:15) —
// the color files the app reads, of TWO KINDS IN TWO FOLDERS UNDER TWO
// DEVICE KEYS ("saving a waveform theme requires saving the entire chrome
// theme as well … one more drop-down that shows what you're picking, whether
// it's the waveform or the chrome"; the picker's scope, color_picker.h):
//   A PALETTE — THE PROGRAM'S TWELVE (kGuiPaletteRoles below; twelve since
//     2026-10-09, when the cues took Cool Edit's two colors and the canvas
//     its grid and center line) and nothing else: `palettes/<name>.palette`,
//     the `palette` device key.
//   A SCHEME — THE CHROME'S TWELVE KEYS (kGuiChromeLines below), Windows'
//     own word for a set of chrome colors (the Appearance dialog's
//     "scheme"), and NO FACE (architect 2026-10-09 ~21:20: "the scheme's
//     default font should stop being honored — it should only be honored
//     from the font picker"; the face is the device config's `font` key,
//     gui_font.h): `schemes/<name>.scheme`, the `scheme` device key.
// The two are independent: loading, saving, renaming or deleting one never
// touches the other, and either applies under any chrome.
//
// TWO SOURCES FOR ONE STRUCT (architect 2026-10-07; 2026-10-08): THE LIVE
// CHROME'S COMPILED THEME (theme_file.h's kGuiChromeThemes, never edited in
// the app) colors the chrome, THE SCHEME'S TWELVE KEYS derived over it by
// the live chrome's own derivation when the active scheme carries them, and
// THE PALETTE colors the program — both kinds picked in the app and saved as
// NAMED presets, the architect's workshop, "save and load a handful of
// themes at a time before we hard code them" (2026-10-08; a look made
// official becomes a chrome variant, its colors compiled in).
// GuiPalette (render.h) keeps every member; install_palette (render.h) fills
// the chrome members off the theme's role table (kGuiThemeRoles) with the
// scheme's keys derived over it and the program members off this file's
// (kGuiPaletteRoles), so every painter reads palette() as before and no
// painter knows which source a color came from. ONE SCHEME SYNTAX UNDER
// EVERY CHROME (architect 2026-10-08 ~11:00 / ~12:10): the same twelve keys
// draw under every chrome, each deriving the rest its own way — under
// windows-2000 the 3D set alone, from the ground, as Windows' Appearance
// dialog derived it (chrome_derive.h, whose live_chrome_words a later
// vocabulary's derivation joins).
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// selected cue label and the flag editor's field and selection band, the
// chrome's selected pair). So the palette's
// program roles are exactly the TWELVE below (the scheme's twelve keys are
// the chrome's, not the program's): the waveform's canvas, ink and lit
// outline; THE CANVAS'S GRID AND CENTER LINE, `grid` and `center`; THE CUES'
// TWO COLORS, `cue` and `range`, and THE INVALID LABEL'S
// PAIR, `invalid_label` and `invalid_label_selected` (architect 2026-10-09
// ~11:50–12:00, Cool Edit's cues: render.h's marker-lane paragraph, the
// table at render.cpp's resolve_flag_face); the playhead's `playhead_stem`
// — its head and its dots alone — and the `scanner`, the white of the
// stems that belong to the controls: the playback line and the zoom
// anchor's stem (architect 2026-10-09: "the playhead is yellow"); and
// THE PANEL'S FACE (the program is Cool Edit from the toolbar down: its
// band, dock bar and row 8 are the program's, so their color is the
// palette's; the role table's paragraph below). (The four flag kinds' faces
// and selected faces, the one flag outline and the one flag label stood
// 2026-10-07 to 2026-10-09; git history.)
//
// THE BUILT-IN PALETTES ARE COOL EDIT'S COLOR PRESETS, COMPILED IN
// (architect 2026-10-09 ~13:30: "only Cool Edit Pro palettes for waveform
// needed now"; the program is Cool Edit under every chrome) —
// kGuiBuiltinPalettes below, the twenty schemes Cool Edit Pro 2.1 carries in
// its executable, in its order, GENERATED into palette_presets.inc by
// tools/theme_catalog/gen_cool_edit_presets.py from the committed text
// tools/theme_catalog/cool_edit_presets.txt (the transcription's rules are
// its head's; regenerate, never hand-edit): each a KEY (`cool-edit-` and the
// scheme's name in the key grammar — the `palette` key's word for it), a
// DISPLAY NAME (Cool Edit's own, "Default" for its "(Default Scheme)") and
// THE TWELVE. THE DEFAULT PALETTE IS ONE UNDER EVERY CHROME, Cool Edit's
// "Default" (`cool-edit-default`, kGuiDefaultPaletteKey, the table's first
// row) — THE PROGRAM'S TWELVE whenever the device config names no palette
// (the per-chrome default palettes of 2026-10-07 to 2026-10-09 retired the
// same day: the program is no chrome's).
//
// THE BUILT-IN SCHEMES (architect 2026-10-08 ~11:00–11:10: "the catalog's
// schemes transcribed to the used keys and compiled in") — kGuiChromeSchemes
// below, EVERY ENTRY OF docs/themes/catalog.json, GENERATED into
// chrome_schemes.inc by tools/theme_catalog/gen_theme_files.py (the
// transcription's rules are its head's; regenerate, never hand-edit): each a
// KEY (the catalog's verbatim — the `scheme` key's word for it), a DISPLAY
// NAME and THE TWELVE CHROME KEYS. A
// SCHEME IS CHROME-ONLY BY ITS KIND: choosing one installs its twelve and
// leaves the program's twelve as they stand (GuiColorPicker::load_preset).
// THE CHROME'S OWN SCHEME — the one its `own_scheme` names (chrome_spec.h:
// `windows-2000-standard`) — CARRIES NO
// KEYS under that chrome (scheme_record): the compiled theme exactly
// (Windows 2000 Standard's gray face is hand-set, chrome_derive.h's head, so
// its scheme run through the rule would not be its own bytes); under another
// chrome it is a scheme like any. A BUILT-IN IS NOT A FILE AND TAKES NO
// FILE: a file bearing a built-in's key is the read's hard fail, and the
// picker never asks the maintenance API below to write, rename or remove
// one (its writers assert it). Nothing is bundled or copied in; each folder
// holds his own files alone. The same holds of THE BUILT-IN PALETTES: a
// palette file bearing a built-in palette's key is the read's hard fail.
//
// THE FILES, BOTH KINDS BESIDE THE DEVICE CONFIG (palette_folder_path,
// scheme_folder_path). THE GRAMMAR is the device config's lexical contract
// exactly (the shared scanner, warptempo_settings::scan_key_value_file:
// LF-terminated `role=value` lines, split at the first '=', no blank line,
// no comment, no whitespace tolerance, no duplicate), each value THE ONE
// COLOR GRAMMAR (theme_colour_word, theme_file.h).
//   A PALETTE FILE NAMES EXACTLY THE TWELVE (architect 2026-10-08; the
//     twelve 2026-10-09): the picker writes all twelve, so a file missing a
//     role is a state the GUI can never produce — the read's first-error hard
//     fail naming the first missing role in the table's order (the
//     two-category rule); a line naming a retired role (the flag faces of
//     2026-10-07 to 2026-10-09 among them) is an unknown role, the read's
//     hard fail on that line, with no migration — the planner edits the
//     devices' files at the install. THE CHROME'S KEYS LEFT THE PALETTE GRAMMAR
//     (2026-10-08 ~18:15): a palette file carrying a chrome key is a state
//     the GUI no longer writes, the read's hard fail on that line, an unknown
//     role (a file of the combined days that carried both halves — split
//     2026-10-08 — is the planner's to edit at the install, no migration).
//   A SCHEME FILE NAMES THE CHROME'S KEYS ALONE (kGuiChromeLines): THE NINE
//     OF THE BLOCK — every key but the inactive caption's three — ALL
//     REQUIRED, the first missing one in the table's order the read's hard
//     fail (the picker writes all nine); THE INACTIVE CAPTION'S THREE EACH
//     OPTIONAL, written once that element has been picked, and absent
//     FOLLOWING the active caption's (GuiChromePick's accessors); a program
//     role in a scheme file is an unknown role, and so is a `font` line (the
//     scheme's optional font line of 2026-10-09 retired the same night with
//     the scheme's face, no migration — the planner edits the devices'
//     files). The picker writes the keys in the table's order (the chooser's
//     order), an unpicked inactive key left out. A
//     SCHEME IS HONORED UNDER EVERY CHROME, whichever saved it: a scheme
//     saved under one chrome stands in the folder when the Settings chrome
//     row switches to another, and draws there by that chrome's derivation.
//     No follower beyond the inactive caption's three: every other role and
//     key stands alone.
// A FILE'S BYTES ARE sRGB (architect 2026-10-08 ~05:15), as every authored
// color's: the hex he lifts from a screenshot or types into the picker, and
// the tablet converts it at the painter's entry (display_transform.h's head).
// Presets picked by eye before that day (a hex then the P3 byte the panel
// showed) are converted once by the planner through the inverse stated
// there.
//
// THE NAME GRAMMAR (is_palette_name_spelling, both kinds): the stem is the
// name verbatim, case-sensitive, 1 to 40 bytes of printable ASCII
// (0x20..0x7E), no leading or trailing space and no '/'. WIDER THAN A
// CATALOG KEY'S hyphen grammar on purpose: a catalog key is a machine key, a
// preset name is TYPED by him through the on-screen keyboard "for
// organization", so it admits capitals, spaces and punctuation; ASCII
// still, TEXT BEING ASCII IN GRAMMARS. The '/' is the one byte a file name
// cannot hold; the suffix is always appended, so no name reaches outside
// the folder. TWO NAME SPACES, ONE PER FOLDER (2026-10-08 ~18:15): a built-in's
// KEY is refused by the launch read (a file named for one is the hard fail
// below), a built-in's DISPLAY NAME by the picker's name commit
// (commit_name, which also refuses a taken file name) — a hand-placed file
// bearing a display name loads; and a palette and a scheme may share a name.
//
// THE FOLDERS ARE READ ONCE AT LAUNCH (read_palette_folder,
// read_scheme_folder, gui_main, before the device config, so its `palette`
// and `scheme` keys are judged against what loaded; the names taken in
// sorted order, so the first error is the same file on every launch) into
// two live maps. A MISSING FOLDER IS NO FILES. EVERY VIOLATION IS THE
// LAUNCH'S FIRST-ERROR HARD FAIL (NO BACKSTOPS FOR ADVERSARIAL USE: a
// violation is a hand edit) — an unknown role (the other kind's key named
// in a file included), a malformed value, a duplicate role, a missing role
// or block key, a name outside the grammar, a file named for a built-in's key,
// an unreadable folder or file — the blunt terminal line naming the file and
// the line and no window, the device config's road.
//
// THE MAPS ARE MAINTAINED AFTER THE LAUNCH BY THE PICKER'S OWN WRITES (the
// picker is the one writer of both folders): write, rename and remove below
// each keep a map and its folder in step, so no folder is ever re-read. THE
// CALLERS (2026-10-07; both kinds 2026-10-08): the launch read, and THE
// COLOR PICKER'S PRESET ACTS (color_picker.h's THE PRESETS: Save and Save As
// write, Rename renames, Delete removes, each on the picker's scope's kind —
// GuiColorPicker::save_preset, commit_name, confirm_delete), its picks
// themselves writing THE LIVE WORDS alone (install_program_palette,
// install_chrome_pick) and no file. None of the writers below touches the
// device config — the `palette` and `scheme` keys are the caller's, which
// owes its own rewrite when it renames or removes the preset a key names
// (GuiColorPicker::write_preset_key). Each answers nothing on success and
// the failure's whole line otherwise, which the caller cards (the
// validation doctrine's class 5).

// THE ROLE TABLE — THE ONE ENUMERATION of the program's roles (architect
// 2026-10-07), in GuiPalette's order: the name a palette file spells and the
// GuiPalette member it fills (render.h's palette block owns what each
// paints). THE TABLE CARRIES NO VALUES (2026-10-09): every built-in palette's
// twelve, the default's among them, are the generated rows of
// kGuiBuiltinPalettes below — Cool Edit's own bytes, one source — where the
// table held a value column per chrome default until that day.
//
// WHAT EACH ROLE TAKES FROM A COOL EDIT SCHEME (the generator's head owns the
// transcription): the canvas its WvBk, the ink its WvFg, THE LIT OUTLINE
// the ink at HLS lightness 0.3157, hue and saturation kept (Cool Edit has no
// such key: the view bar span's shadow rule, cool_edit_derive.h, run once by
// the generator and proven below), `grid` its GrdL, `center` its Cntr, `cue`
// its CueM (the red of the default scheme, F34B58) and `range` its RngM (the
// blue, 4B82F3) — METRICS §4.2 — `playhead_stem` its Curs (the yellow, §4.3),
// THE PANEL'S FACE its Face (Cool Edit's "Dockable Window 3D Color": the
// ground of the program's panel — the toolbar band, the dock bar, row 8 and
// the canvas column's lanes — whose every other tone derives from it,
// cool_edit_derive.h; a scheme recording none takes the default's 626C7B,
// Cool Edit's own fallback); and THE THREE COOL EDIT HAS NO KEY FOR, the
// same in every built-in: the invalid label Windows' dark / bright red of
// the sixteen, `invalid_label` 800000 at rest and `invalid_label_selected`
// FF0000 selected (architect 2026-10-09 ~11:50–12:00, "dimmer unselected,
// brighter selected"), and THE SCANNER WHITE (architect 2026-10-07: "let's
// go back to a white scanner") — THE PLAYHEAD AND THE SCANNER still TWO
// roles, so a palette may part them: `playhead_stem` THE PLAYHEAD'S HEAD AND
// DOTS ALONE, `scanner` THE SCANNER AND THE ZOOM ANCHOR'S STEM, the stems
// that belong to the controls (architect 2026-10-09: "the non-playhead
// stems that are related to the controls should be white, and then the
// playhead is yellow"; render.h's playhead paragraph). `grid` paints the quarter lines of each
// channel's half — the horizontals alone, Cool Edit's verticals not drawn
// (architect 2026-10-09 ~18:40) — and `center` each channel's center line
// over the grid (render_canvas, render.h's row-6 block); Cool Edit's
// boundary line (Bndy) is not drawn and has no role. A
// PALETTE FILE NAMES EVERY ROLE: a file without one is the read's hard fail
// (the two-category rule: the picker writes all twelve).
//
// THE TABLE'S ORDER IS THE PICKER'S CHOOSER (color_picker.cpp's kRoleNames):
// the waveform's three, the canvas's two lines ("Grid", "Center Line"), the
// cues' two colors ("Cue", "Range"), the invalid
// label's pair ("Invalid Label", "Invalid Label Selected"), then the two
// lines that cross the well ("Playhead", "Scanner"), then the panel's face.
// The generator reads the names off this table and refuses to run when they
// are not its own order.
struct GuiPaletteRole {
    const char*            name;
    GuiColor GuiPalette::* member;
};
inline constexpr GuiPaletteRole kGuiPaletteRoles[] = {
    {"waveform_canvas",           &GuiPalette::waveform_canvas},
    {"waveform_ink",              &GuiPalette::waveform_ink},
    {"waveform_outline",          &GuiPalette::waveform_outline},
    {"grid",                      &GuiPalette::grid},
    {"center",                    &GuiPalette::center},
    {"cue",                       &GuiPalette::cue},
    {"range",                     &GuiPalette::range},
    {"invalid_label",             &GuiPalette::invalid_label},
    {"invalid_label_selected",    &GuiPalette::invalid_label_selected},
    {"playhead_stem",             &GuiPalette::playhead_stem},
    {"scanner",                   &GuiPalette::scanner},
    {"face",                      &GuiPalette::face},
};
inline constexpr std::size_t kGuiPaletteRoleCount = std::size(kGuiPaletteRoles);

// ONE PALETTE'S PROGRAM VALUES, as words in the role table's order — what
// install_program_palette takes, a palette file holds and palette_record
// answers.
using GuiPaletteWords = std::array<uint32_t, kGuiPaletteRoleCount>;
// render.h spells this type as `std::array<uint32_t, 12>` (install_palette,
// program_palette_words), since this header includes it; the literal is
// pinned to the table here.
static_assert(std::is_same_v<GuiPaletteWords,
                             std::remove_cvref_t<decltype(program_palette_words())>>,
              "render.h's std::array<uint32_t, 12> must be GuiPaletteWords");

// THE SCHEME'S TWELVE KEYS (the head; architect 2026-10-08 ~11:00, a
// scheme's whole content since ~18:15) — THE ONE ENUMERATION of the chrome's
// keys, in the file's, the chooser's and the generated schemes' order
// (tools/theme_catalog/gen_theme_files.py reads the keys off this table and
// checks its own against them): each the key a scheme file spells,
// GuiChromePick's member it fills (render.h) — a block key's word or an
// inactive key's optional — THE COMPILED ROLE whose word it shows while the
// live scheme carries no keys (the picker's OLD, and the seed of the block a
// first pick creates; the role chrome_derive.h maps the key onto,
// palette_file.cpp's check) and, for an inactive key, the block key it
// FOLLOWS while absent.
inline constexpr std::size_t kGuiChromeNoFollow = static_cast<std::size_t>(-1);
struct GuiChromeLine {
    const char*                          key;
    uint32_t GuiChromePick::*                word;       // a block key's
    std::optional<uint32_t> GuiChromePick::* optional;   // an inactive key's
    const char*                          compiled_role;
    std::size_t                          follows;    // kGuiChromeNoFollow for a block key
};
inline constexpr GuiChromeLine kGuiChromeLines[] = {
    {"chrome_ground",               &GuiChromePick::ground,         nullptr, "ground",                    kGuiChromeNoFollow},
    {"chrome_text",                 &GuiChromePick::text,           nullptr, "label",                     kGuiChromeNoFollow},
    {"chrome_title_start",          &GuiChromePick::title_start,    nullptr, "caption_active",            kGuiChromeNoFollow},
    {"chrome_title_end",            &GuiChromePick::title_end,      nullptr, "caption_active_gradient",   kGuiChromeNoFollow},
    {"chrome_title_text",           &GuiChromePick::title_text,     nullptr, "caption_active_text",       kGuiChromeNoFollow},
    {"chrome_inactive_title_start", nullptr, &GuiChromePick::inactive_title_start, "caption_inactive",          2},
    {"chrome_inactive_title_end",   nullptr, &GuiChromePick::inactive_title_end,   "caption_inactive_gradient", 3},
    {"chrome_inactive_title_text",  nullptr, &GuiChromePick::inactive_title_text,  "caption_inactive_text",     4},
    {"chrome_selection",            &GuiChromePick::selection,      nullptr, "selected_fill",             kGuiChromeNoFollow},
    {"chrome_selection_text",       &GuiChromePick::selection_text, nullptr, "selected_text",             kGuiChromeNoFollow},
    {"chrome_field",                &GuiChromePick::field,          nullptr, "field_ground",              kGuiChromeNoFollow},
    {"chrome_field_text",           &GuiChromePick::field_text,     nullptr, "field_text",                kGuiChromeNoFollow},
};
inline constexpr std::size_t kGuiChromeLineCount = std::size(kGuiChromeLines);

// Whether the key is one of the block's nine (else an inactive key).
constexpr bool is_chrome_block_line(std::size_t line) {
    return kGuiChromeLines[line].word != nullptr;
}
// THE KEY'S WORD IN A PICK: a block key's, or an inactive key's when picked
// and the word of the key it follows otherwise (the inactive caption follows
// the active one, the head).
constexpr uint32_t chrome_line_word(const GuiChromePick& p, std::size_t line) {
    const GuiChromeLine& l = kGuiChromeLines[line];
    if (l.word != nullptr) return p.*(l.word);
    const std::optional<uint32_t>& v = p.*(l.optional);
    return v ? *v : p.*(kGuiChromeLines[l.follows].word);
}
// Write the key's word into a pick (an inactive key becomes picked).
constexpr void set_chrome_line_word(GuiChromePick& p, std::size_t line,
                                    uint32_t rgb) {
    const GuiChromeLine& l = kGuiChromeLines[line];
    if (l.word != nullptr) p.*(l.word) = rgb;
    else                   p.*(l.optional) = rgb;
}

// The index of the role named `name` in the table, or kGuiPaletteRoleCount —
// the reader's one lookup, and install_palette's for the plate's two baked
// inks (waveform_plate_inks).
constexpr std::size_t palette_role_index(std::string_view name) {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        if (name == kGuiPaletteRoles[i].name) return i;
    return kGuiPaletteRoleCount;
}

// INSTALL THE PROGRAM'S TWELVE ALONE (2026-10-07; the install family's
// second member, defined in render.cpp beside install_palette, render.h):
// `words` written into GuiPalette's program members, the plate's two inks
// re-baked and palette_generation bumped, the chrome members untouched — THE
// COMPILED CHROME STAYS LAUNCH-BOUND (it moves only with the chrome, at
// launch, install_palette), the program's colors move live, and so does the
// scheme's twelve, through its own member beside this one
// (install_chrome_pick, render.h, 2026-10-08). THE PICKER'S LIVE ROAD
// (GuiColorPicker::install_live_words, color_picker.h, which calls both and
// then runs this shape once). THE APPLY SHAPE THE
// CALLER OWES, after the call: the caller kicks the waveform when the plate
// inks changed and otherwise refreshes the flag cache alone, then
// invalidates the whole window — the waveform_plate_inks pair read before
// and after the install; when it moved, the synchronous plate rebuild
// (Viewport::kick_waveform_sync — the plate re-rendered in the new inks, the
// flag cache rebuilt at its tail, keyed by the generation); when it did not
// (the grid, the center line, a cue color, an invalid label, the playhead,
// the scanner, the face: no plate render),
// the flag cache alone, synchronously (Viewport::refresh_flag_cache, so a frame
// callback served before the tick cannot blit flags in the old colors); then
// Viewport::invalidate_all, so the swap is one frame.
// WHY THAT IS ENOUGH (re-read 2026-10-09): the twelve bake into two
// cached things alone — the waveform plate, keyed by its two inks
// (waveform_plate_inks, the worker's job carrying them), and the flag cache,
// keyed by the generation (FlagCache::fp_palette_generation), whose rebuild
// also stages the stem stash (AppState::staged_marker_stems, swapped in at
// the publish; it carries which of the two cue colors each stem wears, the
// colors read live); every other reader paints live from palette()
// each frame — the canvas, its grid and its center lines (render_canvas),
// the program's panel (the band, the canvas column's frame,
// row 8, the view bar and its span, the ruler, the marker lane's face), the
// cues' dots (paint_marker_stems), the playhead's head and dots, the open
// flag editor (render_flag_editor_box), the lead-in ring's dots (its stem's
// pair through phase_reset_stem_dots, the two colors read live), the zoom
// anchor's stem and the scanner.
void install_program_palette(const GuiPaletteWords& words);

// THE BUILT-IN PALETTES (the head) — Cool Edit's presets in its order,
// GENERATED (palette_presets.inc; never hand-edited): the key, the display
// name and the twelve in the role table's order. palette_file.cpp asserts
// the keys and display names unique, the keys in the name grammar, the
// default first, and every lit outline the generator's rule off its ink.
struct GuiBuiltinPalette {
    const char*     key;
    const char*     display_name;
    GuiPaletteWords words;
};
inline constexpr GuiBuiltinPalette kGuiBuiltinPalettes[] = {
#include "palette_presets.inc"
};
// THE DEFAULT PALETTE, ONE UNDER EVERY CHROME (the head): Cool Edit's
// "Default", the table's first row — the palette a config with no `palette`
// line takes (effective_palette_name).
inline constexpr const char* kGuiDefaultPaletteKey = "cool-edit-default";

// The built-in palette keyed `name`, byte for byte, or nullptr.
constexpr const GuiBuiltinPalette* builtin_palette(std::string_view name) {
    for (const GuiBuiltinPalette& b : kGuiBuiltinPalettes)
        if (name == b.key) return &b;
    return nullptr;
}
// Whether `name` is a built-in palette's key.
constexpr bool is_builtin_palette_name(std::string_view name) {
    return builtin_palette(name) != nullptr;
}

// THE NAME GRAMMAR (the head), BOTH KINDS: 1 to 40 bytes of printable
// ASCII, no leading or trailing space, no '/'. A file whose stem breaks it
// is the launch's hard fail; a name the picker would save under it is the
// name ask's refusal (the writers' block below).
inline constexpr std::size_t kPaletteNameMaxBytes = 40;
constexpr bool is_palette_name_spelling(std::string_view v) {
    if (v.empty() || v.size() > kPaletteNameMaxBytes) return false;
    if (v.front() == ' ' || v.back() == ' ') return false;
    for (const char c : v)
        if (c < 0x20 || c > 0x7E || c == '/') return false;
    return true;
}

// THE BUILT-IN SCHEMES (the head) — every catalog entry in the catalog's
// order, GENERATED (chrome_schemes.inc; never hand-edited): the key, the
// display name ("Windows Rainy Day", "CDE Northern Sky" — the family's word,
// then the catalog name in Title Case; the generator's head) and the twelve
// keys' words, every inactive key present (a scheme records its inactive
// caption, flat where it records no end). No face: a scheme carries none
// (2026-10-09 ~21:20, the head; the catalog keeps each source's font as
// its record).
struct GuiChromeScheme {
    const char*   key;
    const char*   display_name;
    GuiChromePick chrome;
};
inline constexpr GuiChromeScheme kGuiChromeSchemes[] = {
#include "chrome_schemes.inc"
};

// The built-in scheme keyed `name`, byte for byte, or nullptr.
constexpr const GuiChromeScheme* builtin_scheme(std::string_view name) {
    for (const GuiChromeScheme& b : kGuiChromeSchemes)
        if (name == b.key) return &b;
    return nullptr;
}
// Whether `name` is a built-in scheme's key (the chromes' own among them).
constexpr bool is_builtin_scheme_name(std::string_view name) {
    return builtin_scheme(name) != nullptr;
}
// Whether `name` is a CHROME'S OWN SCHEME'S key (a ChromeSpec's own_scheme,
// chrome_spec.h) — the Chrome scope's first group of built-ins
// (color_picker::preset_menu_rows).
constexpr bool is_chrome_own_scheme(std::string_view name) {
    for (const ChromeSpec* s : kGuiChromeSpecs)
        if (name == s->own_scheme) return true;
    return false;
}

// THE ACTIVE PRESETS THE INSTALL TAKES (architect 2026-10-07; the scheme
// 2026-10-08): the device config's key as written, or — EMPTY, no line —
// for a palette THE DEFAULT PALETTE, `cool-edit-default` under every chrome
// (2026-10-09), and for a scheme THE LIVE CHROME'S OWN (chrome_spec.h's
// own_scheme of live_chrome_spec(): `windows-2000-standard`). The struct
// keeps the empty value, so the file never pins a default.
std::string_view effective_palette_name(std::string_view palette);
std::string_view effective_scheme_name(std::string_view scheme);

// The two folders: `palettes/` and `schemes/` in device_config_path()'s
// folder, or an EMPTY path when the config home does not resolve (the
// config's own load then refuses with its own line).
std::filesystem::path palette_folder_path();
std::filesystem::path scheme_folder_path();

// THE LAUNCH'S ONE READ of each folder (gui_main, before the device config
// is read): every `*.palette` / `*.scheme` regular file parsed under its
// kind's grammar above into its live map. Answers nothing on success and the
// first error's whole line otherwise, the file named — FATAL at the caller.
// Each called once per process.
std::optional<std::string> read_palette_folder();
std::optional<std::string> read_scheme_folder();

// HIS FILES' NAMES of each kind, for the picker's preset menu: the loaded
// files in byte order of their names (the built-ins are kGuiBuiltinPalettes
// and kGuiChromeSchemes, the menu's own order at
// color_picker::preset_menu_rows).
std::vector<std::string> palette_file_names();
std::vector<std::string> scheme_file_names();

// THE `palette` AND `scheme` KEYS' GRAMMARS — each kind's ONE owner, asked by
// the device config's reader (a hand-edited key naming no preset of its
// kind is the launch's hard fail): a built-in's key or a loaded file's name,
// byte for byte — for a palette a built-in palette's key (Cool Edit's
// presets; a catalog scheme's key is the `scheme` key's word alone,
// 2026-10-08 ~18:15), for a scheme any built-in scheme's.
bool is_palette_name(std::string_view name);
bool is_scheme_name(std::string_view name);
inline constexpr const char* kPaletteGrammarReason =
    "must be a built-in palette's key (cool-edit-default, cool-edit-xp-blue, "
    "...) or the name of a palette file read at launch";
inline constexpr const char* kSchemeGrammarReason =
    "must be a built-in scheme's key (windows-2000-standard, "
    "windows-rainy-day, ...) or the name of a scheme file read at launch";

// The twelve the palette `name` names — a built-in palette's row, or a
// loaded file's words (a file names every role, the head). Precondition:
// is_palette_name(name), so this resolves and never refuses (install_palette
// at launch; the picker's load and Delete).
GuiPaletteWords palette_record(std::string_view name);
// The keys the scheme `name` names — for a BUILT-IN its twelve, or NONE when
// it is the live chrome's own scheme (the compiled theme exactly, the head);
// for a loaded file its keys (the nine and each inactive key it carries).
// Precondition: is_scheme_name(name), so this resolves and never refuses.
std::optional<GuiChromePick> scheme_record(std::string_view name);

// THE PICKER'S WRITES (the head: each keeps its map and its folder in step,
// none touches the device config, each answers the failure's whole line).
// THE NAMES ARE JUDGED ONCE, BY THE PICKER, BEFORE ANY WRITER IS ASKED
// (2026-10-07, THE TYPE RULE): its acts gray Save, Rename and Delete under a
// built-in (color_picker::preset_act_enabled) and its name ask refuses a bad
// spelling or a taken name on the card in the product's words
// (GuiColorPicker::commit_name) — so every name a writer receives already
// holds, the writers ASSERT their name preconditions (a breach is a program
// bug) and their one error arm is I/O.
//
// write_palette_file: create or overwrite `<name>.palette` with all twelve
// roles; write_scheme_file: create or overwrite `<name>.scheme` with the
// pick's keys — each uppercase `#RRGGBB` in its table's order, through the
// atomic writer (atomic_write_string_to_path), the folder created on the
// first write. Precondition: `name` in the grammar and no built-in's of the
// kind.
// A SCHEME FILE IS WRITTEN AS THE SCREEN SHOWS IT (2026-10-08, the planner's
// ruling on the scheme split): the nine block keys always, and EACH
// INACTIVE KEY THE PICK CARRIES — so a pick that holds the inactive three
// (a built-in scheme's, every one of which records them; a file's that
// named them; one the picker has picked) is written with all twelve and the
// saved file reproduces the saved look, the laptop's inactive gray staying
// gray after a Save As from windows-2000-standard; the NINE-KEY FORM is the
// file of a pick whose inactive three were never picked and never present,
// the inactive caption then following the active one (the head's rule,
// unchanged). The picker hands this writer the live scheme's keys
// (color_picker::live_scheme_keys: the live pick, or the live chrome's own
// built-in's twelve while it stands, which carries none live). The reader
// takes both forms (read_scheme_file).
std::optional<std::string> write_palette_file(std::string_view name,
                                              const GuiPaletteWords& words);
std::optional<std::string> write_scheme_file(std::string_view name,
                                             const GuiChromePick& pick);
// rename_*_file: `old_name`, a loaded file's, becomes `new_name`.
// Precondition: `new_name` differs, is in the grammar and is no preset's of
// the kind yet (neither a built-in's nor a loaded file's).
std::optional<std::string> rename_palette_file(std::string_view old_name,
                                               std::string_view new_name);
std::optional<std::string> rename_scheme_file(std::string_view old_name,
                                              std::string_view new_name);
// remove_*_file: delete a loaded file of the kind.
std::optional<std::string> remove_palette_file(std::string_view name);
std::optional<std::string> remove_scheme_file(std::string_view name);
