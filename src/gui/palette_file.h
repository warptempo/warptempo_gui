#pragma once

#include "render.h"   // GuiPalette, GuiColor: the role table's members

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

// THE COLORS ARE COMPILED IN, AND ONE KIND OF PRESET REMAINS (architect
// 2026-10-21, "ok to delete other captions"; the fortepiano chrome,
// materials.h):
//   THE WAVEFORM PRESET (identifiers: palette) — THE PROGRAM'S TEN
//     (kGuiPaletteRoles below) and nothing else: one of the built-in
//     presets (kGuiBuiltinPalettes, the program's own thirty-three), the
//     `waveform` device key (named
//     for what it sets since 2026-10-21 — `palette` until 2026-10-14,
//     `chrome` until 2026-10-21, when the preset's Face drove the whole
//     window), set by the settings editor's `waveform=` line, whose Tab
//     completes a typed prefix against the built-in names
//     (GuiSettingsEditor::complete_closed_value) and whose commit installs
//     the preset live (install_program_palette below). It sets THE
//     WAVEFORM'S ROLES ALONE, and its ink's hue THE LIT LEDS' ACCENT
//     (materials.h's accent_of_ink, 2026-10-23): no chrome surface reads its
//     `face` — the panel's Face is the brushed ground's mean (render.cpp's
//     panel_face, materials.h's TONE RULE) — so the role stands in the table
//     and the generated rows as the record of each preset's Face, read by
//     the static_asserts alone.
//   THE CAPTION IS labwc's (render.h's caption note, 2026-10-24): the
//     app paints none (one flat ground and two title inks as constants
//     2026-10-23 to 2026-10-24; the walnut, then the flat ebony,
//     2026-10-21 to 2026-10-23). The
//     caption's six colors and the
//     second kind of preset that set them — the `caption` device key, its
//     Settings Caption row, the product's flat `default` (9B4B2C, the
//     morning of 2026-10-21) and the 42 Windows and Plus! schemes generated
//     from the imported theme catalog (Windows Storm the default
//     2026-10-12..21) — were removed 2026-10-21 with the catalog they were
//     read from.
// THE FAMILIES LANDED ARE WHAT EXISTS (architect 2026-10-14, "remove picker
// and preset file road"): the in-app color picker and its preset FILES — two
// folders beside the device config, read at launch and maintained by the
// picker's Save, Save As, Rename and Delete, 2026-10-07..14 — are removed
// whole; a hand-edited file is adversarial territory.
//
// TWO SOURCES FOR ONE STRUCT: THE PALETTE colors the waveform, and THE
// MATERIALS every chrome role — the hi-fi skin's constants and the ring
// tones off the ground's mean (chrome_derive.h, 2026-10-23). GuiPalette
// (render.h) keeps every member; install_palette (render.h) fills the
// program members off this file's table (kGuiPaletteRoles), the panel's
// `face` member and THE COOL EDIT BLOCK off the panel's Face, and the chrome
// members off the theme's role table (kGuiThemeRoles) by the one derivation
// (chrome_derive.h's live_chrome_words), so every painter reads palette() as
// before and no painter knows which source a color came from.
//
// THE PALETTE'S RULE (architect 2026-10-07 ~10:00): A COLOR THE PROGRAM
// DRAWS IN THE WELL, OR ON A THING THAT ENTERS THE WELL, IS THE PALETTE'S; a
// chrome widget keeps the chrome's colors even where it stands on such a
// thing ("chrome means anything the accent color can highlight": the
// selected cue label and the flag editor's field and selection band, the
// chrome's selected pair). So the palette's program roles are exactly the
// TEN below: the waveform's canvas, ink and lit outline; THE CANVAS'S
// GRID AND CENTER LINE, `grid` and `center`; THE CUE COLOR, `cue`, on both
// of a stem's phases, its contrast over the ink the knockout's (architect
// 2026-10-26, render.h's marker-lane paragraph), and THE INVALID COLOR,
// `invalid_label` (architect 2026-10-11 ~01:00–01:20: an invalid cue's
// triangle, dots and resting label and the `h` view's removed half;
// render.h's marker-lane paragraph, the table at render.cpp's
// resolve_flag_face); the playhead's `playhead_stem` — its head and its dots
// alone — and the `scanner`, the white of the stems that belong to the
// controls: the playback line and the zoom anchor's stem, Cool Edit's own
// constant white (the role table's paragraph below; architect 2026-10-09:
// "the playhead is yellow"); and THE PANEL'S FACE, Cool Edit's (the record,
// above). (The four flag kinds' faces and selected faces, the one flag
// outline and the one flag label stood 2026-10-07 to 2026-10-09; git
// history.)
//
// THE BUILT-IN PALETTES ARE THE PROGRAM'S THIRTY-THREE, COMPILED IN —
// kGuiBuiltinPalettes below, GENERATED into palette_presets.inc by
// tools/theme_catalog/gen_presets.py from one committed text,
// tools/theme_catalog/warptempo_presets.txt (whose head is their design and
// provenance; the transcription's rules are the generator's head's;
// regenerate, never hand-edit), in its order: ONE INK, the DEC VT220's
// green, under thirty-three cue colors, the VGA and Tango palettes mapped
// among them, each with its own invalid red — the cue standing on the
// canvas black alone since the knockout (architect 2026-10-26), so its
// lightness is free (the architect's pick, Terminal, Blue, kept as the
// first row, and the planner's thirty-two beside it; the text's head is the
// design and the families' rules).
// Each a KEY (`warptempo-` and the display name in the key grammar — the
// `waveform` key's word for it), a DISPLAY NAME (the
// source's own) and THE TEN. THE DEFAULT PALETTE is "Terminal, Blue"
// (`warptempo-terminal-blue`, kGuiDefaultPaletteKey; architect 2026-10-26)
// — the one both first-run templates spell
// (`waveform=warptempo-terminal-blue`; every device key required,
// architect 2026-10-14).
// THE BYTES ARE sRGB (architect 2026-10-08 ~05:15), as every authored
// color's, and the tablet converts them at the painter's entry
// (display_transform.h's head).

// THE ROLE TABLE — THE ONE ENUMERATION of the program's roles (architect
// 2026-10-07), in GuiPalette's order: the role's name (the generator's and
// the lookups' key, palette_role_index) and the
// GuiPalette member it fills (render.h's palette block owns what each
// paints). THE TABLE CARRIES NO VALUES (2026-10-09): every built-in palette's
// ten, the default's among them, are the generated rows of
// kGuiBuiltinPalettes below, one source.
//
// WHAT EACH ROLE TAKES FROM A PRESET, recorded in Cool Edit Pro 2.1's value
// grammar (the generator's head owns the transcription): the canvas its
// WvBk, the ink its WvFg, THE LIT OUTLINE the ink at HLS lightness 0.3157,
// hue and saturation kept (no preset records one: the lightness measured on
// Cool Edit's view bar span shadows, cool_edit_derive.h's `span_shadow`
// record — the product's span itself neutral grey since 2026-10-25,
// materials.h — run once by the generator and proven below), `grid` its
// GrdL, `center` its Cntr (A53038 in every built-in, architect 2026-10-25),
// `cue` its CueM (the default's blue, 1CC8FF), `playhead_stem` its Curs
// (Tempest's yellow FFF300 on every built-in; tunable per preset, architect
// 2026-10-25 night),
// THE PANEL'S FACE its Face (Cool Edit's "Dockable Window 3D Color", 626C7B
// in every preset: the record, since the panel is a material's mean on every
// surface since 2026-10-21 — the brushed ground's since 2026-10-23 — the
// head); THE INVALID COLOR `invalid_label` ITS Inv, the one key of the
// program's own, a red tuned per preset apart from its center line (architect
// 2026-10-25 night: "the invalid red we're using was tuned to the default
// theme; it doesn't work that well with any of these. Tune that as well";
// the default's red, F2142E; one red role since 2026-10-11 ~01:00–01:20,
// Windows' dark red 800000 and the selected FF0000 of 2026-10-09 retired,
// the selected role with them: "No flags change color when you select them
// … I'd rather the text just go white, and the flag should be the
// indicator that it's invalid" — a selected invalid label is the chrome's
// text as every selected label's); and THE ONE COOL EDIT HAS NO KEY FOR,
// the same in every built-in: THE SCANNER WHITE, COOL EDIT'S OWN CONSTANT
// (architect 2026-10-16, observed in Cool Edit Pro 2.1 under Wine on the
// laptop: the scanner is white on every preset he tried, so its value
// grammar records no key for it; the zoom anchor's stem is the same design, so both
// are canonical — his rulings of 2026-10-07, "let's go back to a white
// scanner", and 2026-10-09 now its confirmation) — THE PLAYHEAD AND THE SCANNER still TWO
// roles, so a palette may part them: `playhead_stem` THE PLAYHEAD'S HEAD AND
// DOTS ALONE, `scanner` THE SCANNER AND THE ZOOM ANCHOR'S STEM, the stems
// that belong to the controls (architect 2026-10-09: "the non-playhead
// stems that are related to the controls should be white, and then the
// playhead is yellow"; render.h's playhead paragraph). `grid` paints the quarter lines of each
// channel's half — the horizontals alone, Cool Edit's verticals not drawn
// (architect 2026-10-09 ~18:40) — and `center` each channel's center line
// over the waveform (render_center_lines, render.h's row-6 block; the
// stereo divider between the channels wears the panel's tones); Cool Edit's
// boundary line (Bndy) is not drawn and has no role. Every built-in names
// every role (the generator's rows).
//
// THE TABLE'S ORDER (the color picker's chooser's, 2026-10-07..14): the
// waveform's three, the canvas's two lines, the cue color, the
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
// render.h spells this type as `std::array<uint32_t, 10>` (install_palette,
// program_palette_words), since this header includes it; the literal is
// pinned to the table here.
static_assert(std::is_same_v<GuiPaletteWords,
                             std::remove_cvref_t<decltype(program_palette_words())>>,
              "render.h's std::array<uint32_t, 10> must be GuiPaletteWords");

// The index of the role named `name` in the table, or kGuiPaletteRoleCount —
// the reader's one lookup, and install_palette's for the plate's two baked
// inks (waveform_plate_inks).
constexpr std::size_t palette_role_index(std::string_view name) {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        if (name == kGuiPaletteRoles[i].name) return i;
    return kGuiPaletteRoleCount;
}

// INSTALL THE PROGRAM'S TEN (2026-10-07; the install family's second
// member, defined in render.cpp beside install_palette, render.h): `words`
// written into GuiPalette's program members, the plate's two inks re-baked,
// THE CHROME'S MEMBERS RE-DERIVED (chrome_derive.h's live_chrome_words: the
// skin's constants and the ring tones off the ground's mean, so no chrome
// role moves with a preset; the waveform's and the program's roles do, the
// view bar's ink-derived tones among them, and the lit LEDs' accent,
// materials::set_accent)
// and palette_generation bumped. THE LIVE ROAD is the Settings Waveform
// row's commit (GuiSettingsEditor's install_waveform_preset,
// settings_editor.cpp, which calls this and then runs the apply shape once;
// the color picker's install_live_words until 2026-10-14). THE APPLY SHAPE
// THE CALLER OWES, after the call: the caller kicks the waveform when the
// plate inks changed and otherwise refreshes the flag cache alone, then
// invalidates the whole window — the waveform_plate_inks pair read before
// and after the install; when it moved, the synchronous plate rebuild
// (Viewport::kick_waveform_sync — the plate re-rendered in the new inks, the
// flag cache rebuilt at its tail, keyed by the generation); when it did not
// (the grid, the center line, the cue color, the invalid color, the playhead,
// the scanner: no plate render), the flag cache alone, synchronously
// (Viewport::refresh_flag_cache, so a frame callback served before the tick
// cannot blit flags in the old colors); then Viewport::invalidate_all, so
// the swap is one frame (the whole surface, which is the GUI's geometry since
// 2026-10-24: labwc owns the border outside it).
// WHY THAT IS ENOUGH (re-read 2026-10-09): the ten bake into two
// cached things alone — the waveform plate, keyed by its two inks
// (waveform_plate_inks, the worker's job carrying them), and the flag cache,
// keyed by the generation (FlagCache::fp_palette_generation), whose rebuild
// also stages the stem stash (AppState::staged_marker_stems, swapped in at
// the publish; it carries which role each stem's two phases wear, the
// colors read live); every other reader paints live from palette()
// each frame — the canvas, its grid and its center lines (render_canvas),
// the cues' dots (paint_marker_stems), the playhead's
// head and dots, the open flag editor (render_flag_editor_box), the lead-in
// ring's dots (its stem's pair through phase_reset_stem_dots, the colors
// read live), the zoom anchor's stem and the scanner.
void install_program_palette(const GuiPaletteWords& words);

// THE BUILT-IN PALETTES (the head) — the program's thirty-three in their
// text's order, GENERATED (palette_presets.inc; never hand-edited): the key, the display
// name and the ten in the role table's order. palette_file.cpp asserts
// the keys and display names unique, the keys in the key grammar, the
// default a row and every lit outline the generator's rule off its ink.
struct GuiBuiltinPalette {
    const char*     key;
    const char*     display_name;
    GuiPaletteWords words;
};
inline constexpr GuiBuiltinPalette kGuiBuiltinPalettes[] = {
#include "palette_presets.inc"
};
// THE DEFAULT PALETTE (the head): "Terminal, Blue" (architect 2026-10-26,
// the rangeless design "just shipped as the default") — the `waveform=`
// value both first-run templates SPELL (GuiPlatform::device_config_defaults;
// every device key required, architect 2026-10-14 — a config with no line took it
// 2026-10-07..14). A config naming a retired key is the launch's hard fail
// (the 2026-10-14 ruling: no migration).
inline constexpr const char* kGuiDefaultPaletteKey = "warptempo-terminal-blue";

// The built-in palette keyed `name`, byte for byte, or nullptr.
constexpr const GuiBuiltinPalette* builtin_palette(std::string_view name) {
    for (const GuiBuiltinPalette& b : kGuiBuiltinPalettes)
        if (name == b.key) return &b;
    return nullptr;
}
// THE DEFAULT PALETTE'S TEN (the derivation checks' and the install
// family's base).
inline constexpr const GuiPaletteWords& default_palette_words() {
    return builtin_palette(kGuiDefaultPaletteKey)->words;
}
// Whether `name` is a built-in palette's key — THE `waveform` KEY'S GRAMMAR,
// its one owner (the device config's reader and the Waveform row's commit
// ask it; architect 2026-10-14: a built-in's key alone).
constexpr bool is_builtin_palette_name(std::string_view name) {
    return builtin_palette(name) != nullptr;
}

// (THE WAVEFORM ROW'S ORDER — kGuiPaletteMenuOrder, the default pinned
// first and the others alphabetically by display name, architect
// 2026-10-11 — was the color picker's preset menu's and then the Settings
// choice row's domain; it went with the choice drop-downs, architect
// 2026-10-14. The key's Tab completion matches the built-in names in the
// table's own order, GuiSettingsEditor::complete_closed_value.)

// THE ACTIVE PRESET THE INSTALL TAKES IS THE DEVICE CONFIG'S KEY AS WRITTEN
// (architect 2026-10-14: every key required, the default spelled):
// effective_palette_name, which resolved an absent line to the default
// 2026-10-07..14, went with the absent state.

// THE `waveform` KEY'S REFUSAL (the device config's reader and the row's
// commit): a word naming no built-in palette.
inline constexpr const char* kPaletteGrammarReason =
    "must be a built-in waveform preset's key "
    "(warptempo-terminal-blue, warptempo-terminal-ink, ...)";

// The ten the built-in palette `name` names. Precondition:
// is_builtin_palette_name(name), so this resolves and never refuses
// (install_palette at launch; the Waveform row's commit).
GuiPaletteWords palette_record(std::string_view name);
