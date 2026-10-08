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

// THE PALETTE FILES (architect 2026-10-07) — the PROGRAM'S colors, the one
// color file type the app reads (the chrome's colors are compiled in,
// theme_file.h).
//
// TWO SOURCES FOR ONE STRUCT (architect 2026-10-07; 2026-10-08): THE LIVE
// CHROME'S COMPILED THEME (theme_file.h's kGuiChromeThemes, never edited in
// the app) colors the chrome, and THE PALETTE colors the program, picked in
// the app and saved as NAMED presets — the architect's workshop, "save and
// load a handful of themes at a time before we hard code them" (2026-10-08;
// a look made official becomes a chrome variant, its colors compiled in).
// GuiPalette (render.h) keeps every member; install_palette (render.h) fills
// the chrome members off the theme's role table (kGuiThemeRoles) and the
// program members off this file's (kGuiPaletteRoles), so every painter reads
// palette() as before and no painter knows which source a color came from.
// THE CHROME KNOB (architect 2026-10-08 ~11:00, chrome_derive.h's head):
// a palette may also carry THE CHROME'S TWELVE KEYS (kGuiChromeLines below:
// the ground and its text, the caption's start, end and text, the inactive
// caption's three, the selection's fill and text, the field's ground and
// text), honored under windows-2000, where the 3D set alone is derived
// from the ground as Windows' Appearance dialog derived it — the one road by
// which a palette moves the chrome (install_palette with its `chrome`,
// install_chrome_pick live); a palette without them leaves the compiled
// theme exactly its recorded bytes. ONE THEME SYNTAX UNDER EVERY CHROME: the
// same twelve keys apply to any chrome (under clearlooks they are carried,
// unread, until its own derivation lands).
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// playhead's head, WordPad's ruler marker in the chrome's roles, and the flag
// editor's selection band, the chrome's selected pair). So the palette's
// program roles are exactly the FIFTEEN below (the chrome knob's twelve
// keys stand beside them, the chrome's, not the program's): the waveform's canvas, ink and lit
// outline, the four flag kinds' faces and selected faces, the one flag
// outline, the one flag label, the playhead's stem and the scanner.
// THE FLAG OUTLINE IS A ROLE, `flag_outline` (architect 2026-10-08 ~02:30,
// on mock_IM2: "today is the only correct one"): its GEOMETRY is exactly
// today's — the one-W ring round the flag box and its flanks beside the stem
// down the well's top lines, stopping just before the canvas, where the
// outline merges into it (render.h's marker-lane paragraph) — and only its
// COLOR is his to pick, since an outline that is always the canvas is "odd
// when the canvas is anything but black": over a light canvas he may give it
// one of the well frame's tones, so the flag's connection to the canvas
// reads seamless. BOTH DEFAULT PALETTES GIVE IT THEIR CANVAS'S BYTES, so the
// defaults paint exactly as the outline-is-the-canvas rule of 2026-10-07 did
// ("otherwise it will poke out when going into the canvas"). THE FLAG LABEL
// IS ONE for every face, resting and selected (his one-label rule of
// 2026-10-07; the four per-kind selected labels retired the same day).
//
// THE TWO DEFAULT PALETTES ARE COMPILED IN, one per chrome vocabulary, each
// named by its ChromeSpec's `default_palette` (chrome_spec.h):
// `windows-2000-standard` and `clearlooks` — the role table's two value
// columns below, THE PROGRAM'S FIFTEEN whenever the active preset is not a
// file — and each name is ITS CHROME'S OWN BUILT-IN SCHEME's (below).
//
// THE BUILT-IN SCHEMES (architect 2026-10-08 ~11:00–11:10: "the catalog's
// schemes transcribed to the used keys and compiled in") — kGuiChromeSchemes
// below, EVERY ENTRY OF docs/themes/catalog.json, GENERATED into
// chrome_schemes.inc by tools/theme_catalog/gen_theme_files.py (the
// transcription's rules are its head's; regenerate, never hand-edit): each a
// KEY (the catalog's verbatim — the `palette` key's word for it), a DISPLAY
// NAME and THE TWELVE CHROME KEYS. A BUILT-IN IS CHROME-ONLY: choosing one
// installs its twelve and LEAVES THE PROGRAM'S FIFTEEN AS THEY STAND
// (GuiColorPicker::load_palette); at a launch, which has no fifteen standing,
// they are the live chrome's default palette's (palette_record). THE
// CHROME'S OWN SCHEME — the one its `default_palette` names — CARRIES NO
// BLOCK under that chrome: the compiled theme exactly (Windows 2000
// Standard's gray face is hand-set, chrome_derive.h's head, so its scheme run
// through the rule would not be its own bytes); under the other chrome it is
// a scheme like any. A BUILT-IN IS NOT A FILE AND TAKES NO FILE: a file
// bearing a built-in's key is the read's hard fail, and the picker never
// asks the maintenance API below to write, rename or remove one (its writers
// assert it). Nothing is bundled or copied in for palettes; the folder holds
// his own files alone.
//
// THE FILES: `<name>.palette` in the `palettes/` folder BESIDE THE DEVICE
// CONFIG (palette_folder_path). THE GRAMMAR is the device config's lexical
// contract exactly (the shared scanner,
// warptempo_settings::scan_key_value_file: LF-terminated `role=value` lines,
// split at the first '=', no blank line, no comment, no whitespace
// tolerance, no duplicate), each role one of the fifteen and each value THE
// ONE COLOR GRAMMAR (theme_colour_word, theme_file.h). A FILE NAMES EXACTLY
// THE FIFTEEN (architect 2026-10-08, the outline's role): the picker writes
// all fifteen, so a file missing a role is a state the GUI can never
// produce — the read's first-error hard fail naming the first missing role
// in the table's order (the two-category rule). BESIDE THEM, THE CHROME'S
// TWELVE KEYS (architect 2026-10-08 ~11:00, the chrome knob;
// kGuiChromeLines), in the same color grammar: THE NINE OF THE BLOCK —
// every key but the inactive caption's three — ALL PRESENT OR ALL ABSENT:
// the picker's first pick of any chrome element creates all nine from the
// live chrome's words (GuiColorPicker::set_color), so a block missing one is
// a state the GUI never writes — the read's first-error hard fail naming the
// first missing key in the table's order. THE INACTIVE CAPTION'S THREE are
// EACH OPTIONAL, written once that element has been picked, and absent
// FOLLOW the active caption's (GuiChromePick's accessors); one without the
// block is a state the GUI never writes either, the same hard fail naming
// the block's first key. Absent, the live chrome's compiled theme stands
// untouched. The picker writes the keys iff the preset carries the block,
// FIRST IN THE FILE IN THE TABLE'S ORDER (the chooser's order: chrome
// first), an unpicked inactive key left out. THEY ARE HONORED UNDER
// WINDOWS-2000 AND CARRIED, UNREAD, UNDER CLEARLOOKS until its own
// derivation lands — NOT a hard fail there: a palette saved with them under
// windows-2000 stands in the folder when the Settings chrome row switches to
// clearlooks, a state the GUI constructs (chrome_derive.h's head says the
// rest). A preset written while the knob was two keys (`chrome_ground`,
// `chrome_text`, 2026-10-08 ~09:40 to ~11:00) fails on
// `chrome_title_start`, the block's first missing key; the planner edits
// the presets on his devices at the install (no migration), as for a file
// written before the outline became a role, which names fourteen and fails
// on `flag_outline`. No follower beyond the inactive caption's three: every
// other role and key stands alone.
// A FILE'S BYTES ARE sRGB (architect 2026-10-08 ~05:15), as every authored
// color's: the hex he lifts from a screenshot or types into the picker, and
// the tablet converts it at the painter's entry (display_transform.h's head).
// Presets picked by eye before that day (a hex then the P3 byte the panel
// showed) are converted once by the planner through the inverse stated
// there.
//
// THE NAME GRAMMAR (is_palette_name_spelling): the stem is the name
// verbatim, case-sensitive, 1 to 40 bytes of printable ASCII (0x20..0x7E),
// no leading or trailing space and no '/'. WIDER THAN A CATALOG KEY'S
// hyphen grammar on purpose: a catalog key is a machine key, a palette name
// is TYPED by him through the on-screen keyboard "for
// organization", so it admits capitals, spaces and punctuation; ASCII
// still, TEXT BEING ASCII IN GRAMMARS. The '/' is the one byte a file name
// cannot hold; the suffix is always appended, so no name reaches outside
// the folder.
//
// THE FOLDER IS READ ONCE AT LAUNCH (read_palette_folder, gui_main, before
// the device config, so the config's `palette` key is judged against what
// loaded; the names taken in sorted order, so
// the first error is the same file on every launch) into a live map. A
// MISSING FOLDER IS NO FILES. EVERY VIOLATION IS THE LAUNCH'S FIRST-ERROR
// HARD FAIL (NO BACKSTOPS FOR ADVERSARIAL USE: a violation is a hand edit) —
// an unknown role (a chrome role named in a palette file included: the
// chrome's keys are the knob's twelve alone), a malformed value, a duplicate
// role, a missing role, a block missing a key, an inactive key without the
// block, a name outside the grammar, a file named for a built-in, an
// unreadable folder or file — the blunt terminal line
// naming the file and the line and no window, the device config's road.
//
// THE MAP IS MAINTAINED AFTER THE LAUNCH BY THE PICKER'S
// OWN WRITES (the picker is the one writer of the folder): write, rename and
// remove below each keep the map and the folder in step, so the folder is
// never re-read. THE CALLERS (2026-10-07): the launch read, and THE COLOR
// PICKER'S PRESET ACTS (color_picker.h's THE PRESETS: Save and Save As
// write, Rename renames, Delete removes — GuiColorPicker::save_palette,
// commit_name, confirm_delete), its picks themselves writing the LIVE
// PALETTE alone (install_program_palette, install_chrome_pick) and no file. None of the writers
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
    uint32_t               windows_2000;   // 0xRRGGBB, the `windows-2000-standard` default
    uint32_t               clearlooks;     // 0xRRGGBB, the `clearlooks` default
};
// THE `windows-2000-standard` COLUMN is Windows' own: THE WAVEFORM in Windows' twenty
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
// FLAG OUTLINE THE CANVAS'S BLACK (the head: the 2026-10-07 look kept as the
// default). THE ONE FLAG LABEL WHITE on every face, resting and selected — so white stands
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
// red pair; the flag outline the canvas's BLACK (the head); the flag label
// fg_color, BLACK on every face; the playhead's stem and the scanner WHITE,
// his ruling (CL10a).
//
// THE TABLE'S ORDER IS THE PICKER'S CHOOSER (color_picker.cpp's kRoleNames):
// the waveform's three, the four kinds' pairs, then THE TWO FLAG ROLES EVERY
// KIND SHARES — the outline that rings each face and the label that stands
// on it, side by side in the chooser ("Flag Outline", "Flag Label") — then
// the two lines that cross the well.
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
    {"flag_outline",              &GuiPalette::flag_outline,              0x000000, 0x000000},
    {"flag_label",                &GuiPalette::flag_label,                0xFFFFFF, 0x000000},
    {"playhead_stem",             &GuiPalette::playhead_stem,             0xFFFFFF, 0xFFFFFF},
    {"scanner",                   &GuiPalette::scanner,                   0xFFFFFF, 0xFFFFFF},
};
inline constexpr std::size_t kGuiPaletteRoleCount = std::size(kGuiPaletteRoles);

// ONE PALETTE'S PROGRAM VALUES, as words in the role table's order — what
// install_program_palette takes and a GuiPaletteRecord carries.
using GuiPaletteWords = std::array<uint32_t, kGuiPaletteRoleCount>;
// render.h spells this type as `std::array<uint32_t, 15>` (install_palette,
// program_palette_words), since this header includes it; the literal is
// pinned to the table here.
static_assert(std::is_same_v<GuiPaletteWords,
                             std::remove_cvref_t<decltype(program_palette_words())>>,
              "render.h's std::array<uint32_t, 15> must be GuiPaletteWords");

// THE CHROME KNOB'S TWELVE KEYS (the head; architect 2026-10-08 ~11:00) —
// THE ONE ENUMERATION of the chrome's keys, in the file's, the chooser's and
// the generated schemes' order (tools/theme_catalog/gen_theme_files.py reads
// the keys off this table and checks its own against them): each the key a
// palette file spells, GuiChromePick's member it fills (render.h) — a block
// key's word or an inactive key's optional — THE COMPILED ROLE whose word it
// shows while the palette carries no block (the picker's OLD, and the seed
// of the block a first pick creates; the role chrome_derive.h maps the key
// onto, palette_file.cpp's check) and, for an inactive key, the block key it
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

// ONE PALETTE WHOLE: its fifteen words and its optional chrome knob — what
// palette_record answers, what write_palette_file writes and what the
// picker's live road installs (GuiColorPicker::install_live_words).
struct GuiPaletteRecord {
    GuiPaletteWords              words{};
    std::optional<GuiChromePick> chrome;
    bool operator==(const GuiPaletteRecord&) const = default;
};

// THE LIVE PALETTE — the install family's two live halves (render.h's
// program_palette_words and live_chrome_pick) as one record: what Save
// writes and what Save's enabled bit compares.
inline GuiPaletteRecord live_palette_record() {
    return GuiPaletteRecord{program_palette_words(), live_chrome_pick()};
}

// The index of the role named `name` in the table, or kGuiPaletteRoleCount —
// the reader's one lookup, and install_palette's for the plate's two baked
// inks (waveform_plate_inks).
constexpr std::size_t palette_role_index(std::string_view name) {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        if (name == kGuiPaletteRoles[i].name) return i;
    return kGuiPaletteRoleCount;
}

// INSTALL THE PROGRAM'S FIFTEEN ALONE (2026-10-07; the install family's
// second member, defined in render.cpp beside install_palette, render.h):
// `words` written into GuiPalette's program members, the plate's two inks
// re-baked and palette_generation bumped, the chrome members untouched — THE
// COMPILED CHROME STAYS LAUNCH-BOUND (it moves only with the chrome, at
// launch, install_palette), the program's colors move live, and so does the
// chrome knob, through its own member beside this one (install_chrome_pick,
// render.h, 2026-10-08). THE PICKER'S LIVE ROAD
// (GuiColorPicker::install_live_words, color_picker.h, which calls both and
// then runs this shape once). THE APPLY SHAPE THE
// CALLER OWES, after the call: the caller kicks the waveform when the plate
// inks changed and otherwise refreshes the flag cache alone, then
// invalidates the whole window — the waveform_plate_inks pair read before
// and after the install; when it moved, the synchronous plate rebuild
// (Viewport::kick_waveform_sync — the plate re-rendered in the new inks, the
// flag cache rebuilt at its tail, keyed by the generation); when it did not
// (a flag, its outline, the stem, the label, the scanner: no plate render),
// the flag cache alone, synchronously (Viewport::refresh_flag_cache, so a frame
// callback served before the tick cannot blit flags in the old colors); then
// Viewport::invalidate_all, so the swap is one frame.
// WHY THAT IS ENOUGH (re-grepped 2026-10-08): the fifteen bake into two
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
// every ChromeSpec's default_palette names one, that the two orders agree
// and that each name is a built-in scheme's key, the chrome's own).
struct GuiDefaultPalette {
    const char*              name;
    uint32_t GuiPaletteRole::* column;
};
inline constexpr GuiDefaultPalette kGuiDefaultPalettes[] = {
    {"windows-2000-standard", &GuiPaletteRole::windows_2000},
    {"clearlooks",   &GuiPaletteRole::clearlooks},
};

// THE NAME GRAMMAR (the head): 1 to 40 bytes of printable ASCII, no leading
// or trailing space, no '/'. A file whose stem breaks it is the launch's
// hard fail; a name the picker would save under it is the name ask's refusal
// (the writers' block below).
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
// caption, flat where it records no end).
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
// Whether `name` is a built-in's key (the defaults among them).
constexpr bool is_builtin_palette_name(std::string_view name) {
    return builtin_scheme(name) != nullptr;
}

// THE PALETTE THE INSTALL TAKES (architect 2026-10-07): the device config's
// `palette` as written, or — EMPTY, no `palette` line — THE LIVE CHROME'S
// DEFAULT PALETTE (chrome_spec.h's default_palette of live_chrome_spec():
// `windows-2000-standard` or `clearlooks`). The struct keeps the empty value, so the file
// never pins a vocabulary's default.
std::string_view effective_palette_name(std::string_view palette);

// The palettes folder: `palettes/` in device_config_path()'s folder, or an
// EMPTY path when the config home does not resolve (the config's own load
// then refuses with its own line).
std::filesystem::path palette_folder_path();

// THE LAUNCH'S ONE READ of the palettes folder (gui_main, before the device
// config is read): every `*.palette`
// regular file parsed under the grammar above into the live map. Answers
// nothing on success and the first error's whole line otherwise, the file
// named — FATAL at the caller. Called once per process.
std::optional<std::string> read_palette_folder();

// HIS PALETTE FILES' NAMES, for the picker's menu: the loaded files in byte
// order of their names (the built-ins are kGuiChromeSchemes, the menu's
// own order at color_picker::palette_menu_rows).
std::vector<std::string> palette_file_names();

// THE `palette` KEY'S GRAMMAR — the ONE owner, asked by the device config's
// reader (a hand-edited key naming no palette is the launch's hard fail): a
// built-in's key (the defaults among them) or a loaded file's name, byte for
// byte.
bool is_palette_name(std::string_view name);
inline constexpr const char* kPaletteGrammarReason =
    "must be a built-in scheme's key (windows-2000-standard, clearlooks, "
    "windows-rainy-day, ...) or the name of a palette file read at launch";

// The palette `name` names — for a BUILT-IN, the live chrome's default
// palette's fifteen (a built-in carries no program colors, the head) and the
// scheme's twelve keys, NO BLOCK when it is the live chrome's own scheme;
// for a loaded file, its fifteen (a file names every role, the head) and its
// chrome block when it carries one. Precondition: is_palette_name(name), so
// this resolves and never refuses (install_palette at launch; the picker,
// whose load of a built-in keeps the live fifteen instead).
GuiPaletteRecord palette_record(std::string_view name);

// THE PICKER'S WRITES (the head: each keeps the map and the folder in step,
// none touches the device config, each answers the failure's whole line).
// THE NAMES ARE JUDGED ONCE, BY THE PICKER, BEFORE ANY WRITER IS ASKED
// (2026-10-07, THE TYPE RULE): its acts gray Save, Rename and Delete under a
// built-in (color_picker::palette_act_enabled) and its name ask refuses a bad
// spelling or a taken name on the card in the product's words
// (GuiColorPicker::commit_name) — so every name a writer receives already
// holds, the writers ASSERT their name preconditions (a breach is a program
// bug) and their one error arm is I/O.
//
// write_palette_file: create or overwrite `<name>.palette` with the record's
// chrome keys when it carries the block (the nine and every picked inactive
// key, in kGuiChromeLines' order), then all fifteen roles, uppercase
// `#RRGGBB` in the table's order, through the atomic writer
// (atomic_write_string_to_path), the folder created on the first write.
// Precondition: `name` in the grammar and no built-in's.
std::optional<std::string> write_palette_file(std::string_view name,
                                              const GuiPaletteRecord& record);
// rename_palette_file: `old_name`, a loaded file's, becomes `new_name`.
// Precondition: `new_name` differs, is in the grammar and is no palette's
// yet (neither a built-in's nor a loaded file's).
std::optional<std::string> rename_palette_file(std::string_view old_name,
                                               std::string_view new_name);
// remove_palette_file: delete a loaded file's `<name>.palette`.
std::optional<std::string> remove_palette_file(std::string_view name);
