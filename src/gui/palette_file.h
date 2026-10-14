#pragma once

#include "render.h"   // GuiPalette, GuiColor: the role table's members

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

// THE TWO KINDS OF COMPILED-IN PRESET (architect 2026-10-07; split in two
// 2026-10-08 ~18:15; compiled in alone since 2026-10-14) — the colors the
// app installs, of TWO KINDS, EACH CHOSEN BY A SETTINGS CHOICE ROW AND NAMED
// BY ITS DEVICE KEY (kSettingsPopupItems, app_state.h: the Chrome and
// Caption rows; committed at OK alone, settings_editor.h's head).
// THE KINDS' USER WORDS (architect 2026-10-11 ~03:05, go 2026-10-13): the
// program's-eleven kind is called CHROME (through the Face it drives the whole
// window) and the caption's-six kind CAPTION (the old `chrome` device key,
// retired 2026-10-10, returns with this meaning); the identifiers below keep
// `palette` and `scheme` ("scheme" is also the period word for the built-in
// Windows color schemes, "palette" names the role machinery): THE scheme_*
// FAMILY SERVES THE CAPTION KIND AND THE palette_* FAMILY THE CHROME KIND:
//   A CHROME (identifiers: palette) — THE PROGRAM'S ELEVEN (kGuiPaletteRoles
//     below; twelve from 2026-10-09, when the cues took Cool Edit's two colors
//     and the canvas its grid and center line, eleven since 2026-10-11, when
//     the invalid label's selected red retired) and nothing else: one of
//     Cool Edit Pro 2.1's twenty presets (kGuiBuiltinPalettes), the `chrome`
//     device key.
//   A CAPTION (identifiers: scheme) — THE CAPTION'S SIX KEYS (kGuiChromeLines
//     below; the chrome's twelve keys until 2026-10-10, when every other
//     chrome color came to inherit the palette's Cool Edit tones,
//     chrome_derive.h), Windows' own word (COLOR_ACTIVECAPTION; the Appearance
//     dialog's "scheme" for a set of them), and NO FACE (architect 2026-10-09
//     ~21:20: the face is the product's one, gui_font.h): one of the 42
//     Windows and Plus! schemes (kGuiChromeSchemes), the `caption` device key.
// The two are independent: choosing one never touches the other, and
// either applies under any chrome.
// THE FAMILIES LANDED ARE WHAT EXISTS (architect 2026-10-14, "remove picker
// and preset file road": "there won't be anything new … I believe we have
// everything"): the in-app color picker and its preset FILES — two folders
// beside the device config, read at launch and maintained by the picker's
// Save, Save As, Rename and Delete, 2026-10-07..14 — are removed whole; a
// file road without the picker would be a reader with no writer, and a
// hand-edited file is adversarial territory.
//
// TWO SOURCES FOR ONE STRUCT (architect 2026-10-07; 2026-10-08; recast
// 2026-10-10): THE PALETTE colors the program AND, THROUGH COOL EDIT'S
// TONES, EVERY CHROME ROLE BUT THE CAPTION (architect 2026-10-10: "the
// caption remains the only thing that's outside of Cool Edit";
// chrome_derive.h), and THE SCHEME colors the caption — its six keys, or
// with none the compiled caption (kGuiCaptionOwn below).
// GuiPalette (render.h) keeps every member; install_palette (render.h) fills
// the program members off this file's table (kGuiPaletteRoles) and the
// chrome members off the theme's role table (kGuiThemeRoles) by the one
// derivation (chrome_derive.h's live_chrome_words), so every painter reads palette() as before and
// no painter knows which source a color came from. ONE SCHEME SYNTAX UNDER
// EVERY CHROME (architect 2026-10-08 ~11:00 / ~12:10): the same keys draw
// under every chrome.
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// selected cue label and the flag editor's field and selection band, the
// chrome's selected pair — whose colors are themselves the palette's tones
// since 2026-10-10, chrome_derive.h). So the palette's
// program roles are exactly the ELEVEN below (the scheme's keys are the
// caption's, not the program's): the waveform's canvas, ink and lit
// outline; THE CANVAS'S GRID AND CENTER LINE, `grid` and `center`; THE CUES'
// TWO COLORS, `cue` and `range` (architect 2026-10-09 ~11:50–12:00, Cool
// Edit's cues), and THE INVALID COLOR, `invalid_label` (architect 2026-10-11
// ~01:00–01:20: an invalid cue's triangle, dots and resting label and the
// `h` view's removed half; render.h's marker-lane paragraph, the table at
// render.cpp's resolve_flag_face); the playhead's `playhead_stem`
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
// scheme's name in the key grammar — the `chrome` key's word for it), a
// DISPLAY NAME (Cool Edit's own, "Default" for its "(Default Scheme)") and
// THE ELEVEN. THE DEFAULT PALETTE IS ONE UNDER EVERY CHROME, Cool Edit's
// "Midnight" (`cool-edit-midnight`, kGuiDefaultPaletteKey; architect
// 2026-10-12, replacing "Default") — THE PROGRAM'S ELEVEN whenever the device config names no palette
// (the per-chrome default palettes of 2026-10-07 to 2026-10-09 retired the
// same day: the program is no chrome's).
//
// THE BUILT-IN SCHEMES (architect 2026-10-08 ~11:00–11:10: "the catalog's
// schemes transcribed to the used keys and compiled in") — kGuiChromeSchemes
// below, EVERY ENTRY OF docs/themes/catalog.json, GENERATED into
// chrome_schemes.inc by tools/theme_catalog/gen_theme_files.py (the
// transcription's rules are its head's; regenerate, never hand-edit): each a
// KEY (the catalog's verbatim — the `caption` key's word for it), a DISPLAY
// NAME and THE SIX CAPTION KEYS (the product's own `cool-edit-pro-me` of
// 2026-10-10 removed 2026-10-11, "effectively the same now, now that Chrome
// only sets the caption"); the catalog's
// `clearlooks` entry is not among them (architect 2026-10-10, "we can
// remove the Clearlooks theme": the Clearlooks chrome left 2026-10-09; the
// generator's head). A SCHEME IS CAPTION-ONLY BY ITS KIND: choosing one
// installs its six and leaves the program's eleven as they stand
// (install_chrome_pick, at the Caption row's commit). THE CHROME'S OWN
// SCHEME — the one its `own_scheme` names (chrome_spec.h: `windows-storm`) —
// CARRIES NO KEYS under that chrome (scheme_record): the compiled caption
// exactly (kGuiCaptionOwn, the scheme's six by construction), so a config
// naming no scheme and one naming the chrome's own paint alike. THE
// INACTIVE CAPTION'S THREE are each optional in a pick, an absent one
// FOLLOWING the active caption's (GuiChromePick's accessors); every built-in
// records them (the generator's head).
// THE BYTES ARE sRGB (architect 2026-10-08 ~05:15), as every authored
// color's, and the tablet converts them at the painter's entry
// (display_transform.h's head).
//
// THE CHOICE ROWS' ORDERS (architect 2026-10-11, kept for the rows
// 2026-10-14): kGuiPaletteMenuOrder and kGuiSchemeMenuOrder below, each the
// one owner of its row's order.

// THE ROLE TABLE — THE ONE ENUMERATION of the program's roles (architect
// 2026-10-07), in GuiPalette's order: the role's name (the generator's and
// the lookups' key, palette_role_index) and the
// GuiPalette member it fills (render.h's palette block owns what each
// paints). THE TABLE CARRIES NO VALUES (2026-10-09): every built-in palette's
// eleven, the default's among them, are the generated rows of
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
// Cool Edit's own fallback); THE INVALID COLOR `invalid_label` ITS Cntr
// TOO, the preset's own center-line color (architect 2026-10-11 ~01:00–01:20;
// Windows' dark red 800000 and the selected FF0000 of 2026-10-09 retired,
// the selected role with them: "No flags change color when you select them
// … I'd rather the text just go white, and the flag should be the
// indicator that it's invalid" — a selected invalid label is the chrome's
// text as every selected label's; still a role of its own, so a palette
// may part it from the center line); and THE ONE COOL EDIT HAS NO KEY FOR,
// the same in every built-in: THE SCANNER WHITE (architect 2026-10-07:
// "let's go back to a white scanner") — THE PLAYHEAD AND THE SCANNER still TWO
// roles, so a palette may part them: `playhead_stem` THE PLAYHEAD'S HEAD AND
// DOTS ALONE, `scanner` THE SCANNER AND THE ZOOM ANCHOR'S STEM, the stems
// that belong to the controls (architect 2026-10-09: "the non-playhead
// stems that are related to the controls should be white, and then the
// playhead is yellow"; render.h's playhead paragraph). `grid` paints the quarter lines of each
// channel's half — the horizontals alone, Cool Edit's verticals not drawn
// (architect 2026-10-09 ~18:40) — and `center` each channel's center line
// over the grid (render_canvas, render.h's row-6 block); Cool Edit's
// boundary line (Bndy) is not drawn and has no role. Every built-in names
// every role (the generator's rows).
//
// THE TABLE'S ORDER (the color picker's chooser's, 2026-10-07..14): the
// waveform's three, the canvas's two lines, the cues' two colors, the
// invalid color, then the two lines that cross the well (the playhead, the
// scanner), then the panel's face. The generator reads the names off this
// table and refuses to run when they are not its own order.
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
    {"playhead_stem",             &GuiPalette::playhead_stem},
    {"scanner",                   &GuiPalette::scanner},
    {"face",                      &GuiPalette::face},
};
inline constexpr std::size_t kGuiPaletteRoleCount = std::size(kGuiPaletteRoles);

// ONE PALETTE'S PROGRAM VALUES, as words in the role table's order — what
// install_program_palette takes, a built-in palette holds and palette_record
// answers.
using GuiPaletteWords = std::array<uint32_t, kGuiPaletteRoleCount>;
// render.h spells this type as `std::array<uint32_t, 11>` (install_palette,
// program_palette_words), since this header includes it; the literal is
// pinned to the table here.
static_assert(std::is_same_v<GuiPaletteWords,
                             std::remove_cvref_t<decltype(program_palette_words())>>,
              "render.h's std::array<uint32_t, 11> must be GuiPaletteWords");

// THE SCHEME'S SIX KEYS — THE CAPTION (the head; architect 2026-10-08
// ~11:00, a scheme's whole content since ~18:15; the caption alone since
// 2026-10-10) — THE ONE ENUMERATION of the scheme's keys, in the generated
// schemes' order
// (tools/theme_catalog/gen_theme_files.py reads the keys off this table and
// checks its own against them): each the key's name,
// GuiChromePick's member it fills (render.h) — a block key's word or an
// inactive key's optional — THE CHROME ROLE it moves (chrome_derive.h maps
// it there, palette_file.cpp's check) and, for an inactive key, the block
// key it FOLLOWS while absent.
inline constexpr std::size_t kGuiChromeNoFollow = static_cast<std::size_t>(-1);
struct GuiChromeLine {
    const char*                              key;
    uint32_t GuiChromePick::*                word;       // a block key's
    std::optional<uint32_t> GuiChromePick::* optional;   // an inactive key's
    const char*                              role;
    std::size_t                              follows;    // kGuiChromeNoFollow for a block key
};
inline constexpr GuiChromeLine kGuiChromeLines[] = {
    {"chrome_title_start",          &GuiChromePick::title_start, nullptr, "caption_active",          kGuiChromeNoFollow},
    {"chrome_title_end",            &GuiChromePick::title_end,   nullptr, "caption_active_gradient", kGuiChromeNoFollow},
    {"chrome_title_text",           &GuiChromePick::title_text,  nullptr, "caption_active_text",     kGuiChromeNoFollow},
    {"chrome_inactive_title_start", nullptr, &GuiChromePick::inactive_title_start, "caption_inactive",          0},
    {"chrome_inactive_title_end",   nullptr, &GuiChromePick::inactive_title_end,   "caption_inactive_gradient", 1},
    {"chrome_inactive_title_text",  nullptr, &GuiChromePick::inactive_title_text,  "caption_inactive_text",     2},
};
inline constexpr std::size_t kGuiChromeLineCount = std::size(kGuiChromeLines);

// Whether the key is one of the block's three (else an inactive key).
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

// INSTALL THE PROGRAM'S ELEVEN (2026-10-07; the install family's second
// member, defined in render.cpp beside install_palette, render.h): `words`
// written into GuiPalette's program members, the plate's two inks re-baked,
// THE CHROME'S MEMBERS RE-DERIVED FROM THEM under the live scheme's caption
// (2026-10-10: every chrome role but the caption's six is a tone of the
// palette's face or ink, chrome_derive.h's live_chrome_words) and
// palette_generation bumped — so a new Face or ink moves the
// chrome live; the caption's six move through their own member beside this
// one (install_chrome_pick, render.h, 2026-10-08). THE LIVE ROAD is the
// Settings Chrome and Caption rows' commit (GuiSettingsEditor's
// install_color_preset, settings_editor.cpp, which calls both and then runs
// this shape once; the color picker's install_live_words until 2026-10-14).
// THE APPLY SHAPE THE
// CALLER OWES, after the call: the caller kicks the waveform when the plate
// inks changed and otherwise refreshes the flag cache alone, then
// invalidates the whole window (the whole SURFACE when the chrome's words
// moved: the restored laptop's sizing frame paints in them, outside the
// client) — the waveform_plate_inks pair read before
// and after the install; when it moved, the synchronous plate rebuild
// (Viewport::kick_waveform_sync — the plate re-rendered in the new inks, the
// flag cache rebuilt at its tail, keyed by the generation); when it did not
// (the grid, the center line, a cue color, the invalid color, the playhead,
// the scanner, the face: no plate render),
// the flag cache alone, synchronously (Viewport::refresh_flag_cache, so a frame
// callback served before the tick cannot blit flags in the old colors); then
// Viewport::invalidate_all, so the swap is one frame.
// WHY THAT IS ENOUGH (re-read 2026-10-09): the eleven bake into two
// cached things alone — the waveform plate, keyed by its two inks
// (waveform_plate_inks, the worker's job carrying them), and the flag cache,
// keyed by the generation (FlagCache::fp_palette_generation), whose rebuild
// also stages the stem stash (AppState::staged_marker_stems, swapped in at
// the publish; it carries which of the cue colors each stem's two phases
// wear, the colors read live); every other reader paints live from palette()
// each frame — the canvas, its grid and its center lines (render_canvas),
// the program's panel (the band, the canvas column's frame,
// row 8, the view bar and its span, the ruler, the marker lane's face), the
// cues' dots (paint_marker_stems), the playhead's head and dots, the open
// flag editor (render_flag_editor_box), the lead-in ring's dots (its stem's
// pair through phase_reset_stem_dots, the colors read live), the zoom
// anchor's stem and the scanner.
void install_program_palette(const GuiPaletteWords& words);

// THE BUILT-IN PALETTES (the head) — Cool Edit's presets in its order,
// GENERATED (palette_presets.inc; never hand-edited): the key, the display
// name and the eleven in the role table's order. palette_file.cpp asserts
// the keys and display names unique, the keys in the key grammar, the
// default a row, every lit outline the generator's rule off its ink and
// every invalid color its center line.
struct GuiBuiltinPalette {
    const char*     key;
    const char*     display_name;
    GuiPaletteWords words;
};
inline constexpr GuiBuiltinPalette kGuiBuiltinPalettes[] = {
#include "palette_presets.inc"
};
// THE DEFAULT PALETTE, ONE UNDER EVERY CHROME (the head): Cool Edit's
// "Midnight" (architect 2026-10-12; "Default" is one of the others now) —
// the palette a config with no `chrome` line takes
// (effective_palette_name).
inline constexpr const char* kGuiDefaultPaletteKey = "cool-edit-midnight";

// The built-in palette keyed `name`, byte for byte, or nullptr.
constexpr const GuiBuiltinPalette* builtin_palette(std::string_view name) {
    for (const GuiBuiltinPalette& b : kGuiBuiltinPalettes)
        if (name == b.key) return &b;
    return nullptr;
}
// THE DEFAULT PALETTE'S ELEVEN (the derivation checks' and the install
// family's base).
inline constexpr const GuiPaletteWords& default_palette_words() {
    return builtin_palette(kGuiDefaultPaletteKey)->words;
}
// Whether `name` is a built-in palette's key — THE `chrome` KEY'S GRAMMAR,
// its one owner (the device config's reader and the Chrome row's commit
// ask it; architect 2026-10-14: a built-in's key alone).
constexpr bool is_builtin_palette_name(std::string_view name) {
    return builtin_palette(name) != nullptr;
}

// THE MENU ORDER'S NAME COMPARE (architect 2026-10-11, the alphabetical
// rule): case-insensitive by the display name (ASCII), ties by bytes so the
// order is total.
constexpr bool menu_name_less(std::string_view a, std::string_view b) {
    const auto lower = [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    const bool less = std::ranges::lexicographical_compare(
        a, b, {}, lower, lower);
    const bool more = std::ranges::lexicographical_compare(
        b, a, {}, lower, lower);
    return less || (!more && a < b);
}

// THE CHROME ROW'S ORDER — ITS ONE OWNER (architect 2026-10-11 for the
// color picker's preset menu, kept for the Settings Chrome row 2026-10-14):
// indices into kGuiBuiltinPalettes, THE DEFAULT (Midnight since 2026-10-12)
// PINNED FIRST ("because it's called default, we should support it as a
// default"), then the other nineteen ALPHABETICALLY BY THEIR DISPLAY NAMES
// (menu_name_less). Read by the row's domain (kChromeChoiceSource,
// app_state.h).
inline constexpr auto kGuiPaletteMenuOrder = [] {
    std::array<std::size_t, std::size(kGuiBuiltinPalettes)> order{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < order.size(); ++i)
        if (std::string_view(kGuiBuiltinPalettes[i].key) == kGuiDefaultPaletteKey)
            order[n++] = i;
    for (std::size_t i = 0; i < order.size(); ++i)
        if (std::string_view(kGuiBuiltinPalettes[i].key) != kGuiDefaultPaletteKey)
            order[n++] = i;
    std::sort(order.begin() + 1, order.end(),
              [](std::size_t a, std::size_t b) {
                  return menu_name_less(kGuiBuiltinPalettes[a].display_name,
                                        kGuiBuiltinPalettes[b].display_name);
              });
    return order;
}();

// THE BUILT-IN SCHEMES (the head) — every windows and windows-plus catalog
// entry in the catalog's order, GENERATED (chrome_schemes.inc; never hand-edited): the key, the
// display name ("Windows Rainy Day", "Plus Space" — the family's word,
// then the catalog name in Title Case; the generator's head) and the six
// caption keys' words, every inactive key present (a scheme records its inactive
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
// THE COMPILED CAPTION (theme_file.h's head): the chrome's own scheme's six
// keys, read from the generated table (a compile error if the key names no
// row). THE ONE RESOLVER — read by the derivation's live words
// (live_chrome_words, chrome_derive.h).
inline constexpr GuiChromePick kGuiCaptionOwn =
    builtin_scheme(kChromeSpecWin2000.own_scheme)->chrome;
inline constexpr const GuiChromePick& chrome_caption() {
    return kGuiCaptionOwn;
}
// Whether `name` is a built-in scheme's key (the chromes' own among them) —
// THE `caption` KEY'S GRAMMAR, its one owner (the device config's reader and
// the Caption row's commit ask it; architect 2026-10-14: a built-in's key
// alone).
constexpr bool is_builtin_scheme_name(std::string_view name) {
    return builtin_scheme(name) != nullptr;
}
// Whether `name` is a CHROME'S OWN SCHEME'S key (a ChromeSpec's own_scheme,
// chrome_spec.h) — the Caption row's first row (kGuiSchemeMenuOrder).
constexpr bool is_chrome_own_scheme(std::string_view name) {
    return name == kChromeSpecWin2000.own_scheme;
}

// THE CAPTION ROW'S ORDER — ITS ONE OWNER (architect 2026-10-08 ~19:30 and
// 2026-10-11 for the color picker's preset menu, kept for the Settings
// Caption row 2026-10-14): indices into kGuiChromeSchemes, THE CHROME'S OWN
// SCHEME FIRST, then every other scheme GROUPED BY FAMILY IN THE CATALOG'S
// ORDER (his word: the built-in schemes are not alphabetized). Read by the
// row's domain (kCaptionChoiceSource, app_state.h).
inline constexpr auto kGuiSchemeMenuOrder = [] {
    std::array<std::size_t, std::size(kGuiChromeSchemes)> order{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < order.size(); ++i)
        if (is_chrome_own_scheme(kGuiChromeSchemes[i].key)) order[n++] = i;
    for (std::size_t i = 0; i < order.size(); ++i)
        if (!is_chrome_own_scheme(kGuiChromeSchemes[i].key)) order[n++] = i;
    return order;
}();

// THE ACTIVE PRESETS THE INSTALL TAKES (architect 2026-10-07; the scheme
// 2026-10-08): the device config's key as written, or — EMPTY, no line —
// for a palette THE DEFAULT PALETTE, `cool-edit-midnight` under every chrome
// (2026-10-12), and for a scheme THE LIVE CHROME'S OWN (chrome_spec.h's
// own_scheme of live_chrome_spec(): `windows-storm`). The struct
// keeps the empty value, so the file never pins a default.
std::string_view effective_palette_name(std::string_view palette);
std::string_view effective_scheme_name(std::string_view scheme);

// THE `chrome` AND `caption` KEYS' REFUSALS (the device config's reader and
// the choice rows' commit): a word naming no built-in of its kind.
inline constexpr const char* kPaletteGrammarReason =
    "must be a built-in chrome's key (cool-edit-midnight, cool-edit-xp-blue, "
    "...)";
inline constexpr const char* kSchemeGrammarReason =
    "must be a built-in caption's key (windows-storm, windows-rainy-day, ...)";

// The eleven the built-in palette `name` names. Precondition:
// is_builtin_palette_name(name), so this resolves and never refuses
// (install_palette at launch; the Chrome row's commit).
GuiPaletteWords palette_record(std::string_view name);
// The keys the built-in scheme `name` names — its six, or NONE when it is
// the live chrome's own scheme (the compiled caption exactly, the head).
// Precondition: is_builtin_scheme_name(name), so this resolves and never
// refuses (install_palette at launch; the Caption row's commit).
std::optional<GuiChromePick> scheme_record(std::string_view name);
