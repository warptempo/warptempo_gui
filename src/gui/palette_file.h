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
//   A PALETTE — THE PROGRAM'S FIFTEEN (kGuiPaletteRoles below) and nothing
//     else: `palettes/<name>.palette`, the `palette` device key.
//   A SCHEME — THE CHROME'S TWELVE KEYS (kGuiChromeLines below), Windows'
//     own word for a set of chrome colors (the Appearance dialog's
//     "scheme"), AND ITS FACE TAG (architect 2026-10-09: the face follows
//     the scheme, as a Windows scheme carries its font — Tahoma or MS Sans
//     Serif, which the windows chrome wears live, gui_font.h's
//     gui_live_face_set; clearlooks and cde keep their own faces):
//     `schemes/<name>.scheme`, the `scheme` device key.
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
// dialog derived it (chrome_derive.h); under clearlooks every tone, by GTK's
// and metacity's own arithmetic, the two title ends ignored
// (clearlooks_derive.h); under cde Motif's XmGetColors (cde_derive.h).
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// playhead's head, WordPad's ruler marker in the chrome's roles, and the flag
// editor's selection band, the chrome's selected pair). So the palette's
// program roles are exactly the FIFTEEN below (the scheme's twelve keys are
// the chrome's, not the program's): the waveform's canvas, ink and lit
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
// reads seamless. EVERY DEFAULT PALETTE GIVES IT ITS CANVAS'S BYTES, so the
// defaults paint exactly as the outline-is-the-canvas rule of 2026-10-07 did
// ("otherwise it will poke out when going into the canvas"). THE FLAG LABEL
// IS ONE for every face, resting and selected (his one-label rule of
// 2026-10-07; the four per-kind selected labels retired the same day).
//
// THE BUILT-IN PALETTES ARE THE DEFAULT PALETTES, COMPILED IN, one per
// chrome vocabulary, each named by its ChromeSpec's `default_palette`
// (chrome_spec.h): `windows-2000-standard`, `clearlooks` and `solaris` — the
// role table's two value columns below (the cde chrome's `solaris` takes
// Windows 2000's column until he authors its own, 2026-10-08) — THE
// PROGRAM'S FIFTEEN whenever the active palette is not a file. Each default
// palette's name IS ITS CHROME'S OWN BUILT-IN SCHEME'S KEY (below), so one
// word names a chrome's own colors in both kinds, and a default palette is
// shown by that scheme's display name ("Windows 2000 Standard").
//
// THE BUILT-IN SCHEMES (architect 2026-10-08 ~11:00–11:10: "the catalog's
// schemes transcribed to the used keys and compiled in") — kGuiChromeSchemes
// below, EVERY ENTRY OF docs/themes/catalog.json, GENERATED into
// chrome_schemes.inc by tools/theme_catalog/gen_theme_files.py (the
// transcription's rules are its head's; regenerate, never hand-edit), and the
// cde chrome's hand-set `solaris`: each a KEY (the catalog's verbatim — the
// `scheme` key's word for it), a DISPLAY NAME and THE TWELVE CHROME KEYS. A
// SCHEME IS CHROME-ONLY BY ITS KIND: choosing one installs its twelve and
// leaves the program's fifteen as they stand (GuiColorPicker::load_preset).
// THE CHROME'S OWN SCHEME — the one its `default_palette` names — CARRIES NO
// KEYS under that chrome (scheme_record): the compiled theme exactly
// (Windows 2000 Standard's gray face is hand-set, chrome_derive.h's head, so
// its scheme run through the rule would not be its own bytes); under another
// chrome it is a scheme like any. A BUILT-IN IS NOT A FILE AND TAKES NO
// FILE: a file bearing a built-in's key is the read's hard fail, and the
// picker never asks the maintenance API below to write, rename or remove
// one (its writers assert it). Nothing is bundled or copied in; each folder
// holds his own files alone.
//
// THE FILES, BOTH KINDS BESIDE THE DEVICE CONFIG (palette_folder_path,
// scheme_folder_path). THE GRAMMAR is the device config's lexical contract
// exactly (the shared scanner, warptempo_settings::scan_key_value_file:
// LF-terminated `role=value` lines, split at the first '=', no blank line,
// no comment, no whitespace tolerance, no duplicate), each value THE ONE
// COLOR GRAMMAR (theme_colour_word, theme_file.h).
//   A PALETTE FILE NAMES EXACTLY THE FIFTEEN (architect 2026-10-08, the
//     outline's role): the picker writes all fifteen, so a file missing a
//     role is a state the GUI can never produce — the read's first-error hard
//     fail naming the first missing role in the table's order (the
//     two-category rule). THE CHROME'S KEYS LEFT THE PALETTE GRAMMAR
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
//     role in a scheme file is an unknown role. THE FONT LINE IS OPTIONAL
//     (kGuiSchemeFontKey below: `font=tahoma` or `font=ms-sans-serif`,
//     absent tahoma, any other word the hard fail). The picker writes the
//     keys in the table's order (the chooser's order), an unpicked inactive
//     key left out, then the font line when the tag is MS Sans Serif. A
//     SCHEME IS HONORED UNDER EVERY CHROME, whichever saved it: a
//     scheme saved under windows-2000 stands in the folder when the Settings
//     chrome row switches to clearlooks, and draws there by clearlooks'
//     derivation. No follower beyond the inactive caption's three: every
//     other role and key stands alone.
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
// install_program_palette takes, a palette file holds and palette_record
// answers.
using GuiPaletteWords = std::array<uint32_t, kGuiPaletteRoleCount>;
// render.h spells this type as `std::array<uint32_t, 15>` (install_palette,
// program_palette_words), since this header includes it; the literal is
// pinned to the table here.
static_assert(std::is_same_v<GuiPaletteWords,
                             std::remove_cvref_t<decltype(program_palette_words())>>,
              "render.h's std::array<uint32_t, 15> must be GuiPaletteWords");

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

// THE SCHEME'S FONT LINE (architect 2026-10-09: the face follows the
// scheme): `font=tahoma` or `font=ms-sans-serif`, OPTIONAL — absent is
// tahoma — after the keys, the pick's face tag (GuiChromePick::face,
// render.h; GuiSchemeFace, gui_font.h). Any other word is the read's hard
// fail (the two-category rule: the picker writes only these two); in a
// palette file the line is an unknown role, the read's hard fail as any
// chrome line is. The picker writes it when the pick's tag is MS Sans Serif
// and leaves it out otherwise, as it leaves out an unpicked inactive key
// (write_scheme_file below) — so a scheme saved from Windows 2000 Standard
// keeps the twelve-line form it always had.
inline constexpr const char* kGuiSchemeFontKey = "font";
struct GuiSchemeFontWord {
    GuiSchemeFace face;
    const char*   word;
};
inline constexpr GuiSchemeFontWord kGuiSchemeFontWords[] = {
    {GuiSchemeFace::Tahoma,      "tahoma"},
    {GuiSchemeFace::MsSansSerif, "ms-sans-serif"},
};
constexpr std::optional<GuiSchemeFace> scheme_font_of_word(std::string_view w) {
    for (const GuiSchemeFontWord& f : kGuiSchemeFontWords)
        if (w == f.word) return f.face;
    return std::nullopt;
}
constexpr const char* scheme_font_word(GuiSchemeFace face) {
    for (const GuiSchemeFontWord& f : kGuiSchemeFontWords)
        if (f.face == face) return f.word;
    return "tahoma";
}

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

// INSTALL THE PROGRAM'S FIFTEEN ALONE (2026-10-07; the install family's
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

// THE DEFAULT PALETTES — THE BUILT-IN PALETTES, each a name and its column
// of the role table, in the vocabularies' order (kGuiChromeSpecs;
// palette_file.cpp asserts that every ChromeSpec's default_palette names
// one, that the two orders agree and that each name is a built-in scheme's
// key, the chrome's own).
struct GuiDefaultPalette {
    const char*              name;
    uint32_t GuiPaletteRole::* column;
};
inline constexpr GuiDefaultPalette kGuiDefaultPalettes[] = {
    {"windows-2000-standard", &GuiPaletteRole::windows_2000},
    {"clearlooks",   &GuiPaletteRole::clearlooks},
    // CDE'S DEFAULT PALETTE IS WINDOWS 2000'S COLUMN (2026-10-08, the cde
    // arc's brief: the fifteen program roles the base's — the waveform lime
    // on black and the rest — "until he authors one in the picker"; the
    // recipe unchanged, a third column joining this table when he does).
    {"solaris",      &GuiPaletteRole::windows_2000},
};

// The default palette named `name`, byte for byte, or nullptr.
constexpr const GuiDefaultPalette* default_palette(std::string_view name) {
    for (const GuiDefaultPalette& d : kGuiDefaultPalettes)
        if (name == d.name) return &d;
    return nullptr;
}
// Whether `name` is a built-in palette's — a default palette's (the head:
// the defaults are the built-in palettes, and nothing else is).
constexpr bool is_builtin_palette_name(std::string_view name) {
    return default_palette(name) != nullptr;
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
// caption, flat where it records no end), with THE FACE TAG its source
// names (2026-10-09; the generator's head: the menu font a Windows source
// records — Tahoma for Tahoma, MS Sans Serif for every other face — and
// Tahoma, inert, for a family whose source names no Windows font).
struct GuiChromeScheme {
    const char*   key;
    const char*   display_name;
    GuiChromePick chrome;
};
inline constexpr GuiChromeScheme kGuiChromeSchemes[] = {
#include "chrome_schemes.inc"
    // THE SOLARIS SCHEME, HAND-SET AFTER THE GENERATED ROWS (2026-10-08): the
    // cde chrome's own built-in, no catalog entry (Sun's Default.dp is not in
    // cdesktopenv; theme_file.h's kGuiThemeCde head owns its provenance —
    // NsCDE's Solyaris.dp transcription, the captures the law) — its twelve
    // keys the compiled theme's words key for key (palette_file.cpp's
    // defaults_are_their_chromes_schemes proves it): the body and black, the
    // flat title under white, the inactive three the body under black, the
    // inverted list selection, set 4's cream under black. The display name
    // follows the generator's rule for a CDE entry ("CDE Solaris"; "KDE 3
    // Solaris" is TDE's own transcription of the same palette).
    {"solaris", "CDE Solaris", {0xAEB2C3, 0x000000, 0xB24D7A, 0xB24D7A, 0xFFFFFF, 0xAEB2C3, 0xAEB2C3, 0x000000, 0x000000, 0xFFF7E9, 0xFFF7E9, 0x000000}},
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

// THE ACTIVE PRESETS THE INSTALL TAKES (architect 2026-10-07; the scheme
// 2026-10-08): the device config's key as written, or — EMPTY, no line —
// THE LIVE CHROME'S OWN (chrome_spec.h's default_palette of
// live_chrome_spec(): `windows-2000-standard`, `clearlooks` or `solaris`,
// which names the chrome's default palette and its own scheme alike). The
// struct keeps the empty value, so the file never pins a vocabulary's
// default.
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
// files in byte order of their names (the built-ins are kGuiDefaultPalettes
// and kGuiChromeSchemes, the menu's own order at
// color_picker::preset_menu_rows).
std::vector<std::string> palette_file_names();
std::vector<std::string> scheme_file_names();

// THE `palette` AND `scheme` KEYS' GRAMMARS — each kind's ONE owner, asked by
// the device config's reader (a hand-edited key naming no preset of its
// kind is the launch's hard fail): a built-in's key or a loaded file's name,
// byte for byte — for a palette a default palette's key (a catalog scheme's
// key is the `scheme` key's word alone, 2026-10-08 ~18:15), for a scheme any
// built-in scheme's.
bool is_palette_name(std::string_view name);
bool is_scheme_name(std::string_view name);
inline constexpr const char* kPaletteGrammarReason =
    "must be a default palette's key (windows-2000-standard, clearlooks, "
    "solaris) or the name of a palette file read at launch";
inline constexpr const char* kSchemeGrammarReason =
    "must be a built-in scheme's key (windows-2000-standard, clearlooks, "
    "solaris, windows-rainy-day, ...) or the name of a scheme file read at "
    "launch";

// The fifteen the palette `name` names — a default palette's column, or a
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
// write_palette_file: create or overwrite `<name>.palette` with all fifteen
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
// takes both forms (read_scheme_file). THE FONT LINE follows the keys when
// the pick's face tag is MS Sans Serif — the scheme's own tag, written under
// whichever chrome saves it, as its keys are (a scheme is honored under
// every chrome; only the windows chrome reads the tag).
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
