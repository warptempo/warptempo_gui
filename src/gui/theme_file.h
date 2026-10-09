#pragma once

#include "render.h"   // GuiPalette, GuiColor: the role table's members;
                      // through gui_font.h, chrome_spec.h's ChromeSpec

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// THE CHROME'S COLORS (architect 2026-10-04; COMPILED IN since 2026-10-08)
// — every color the CHROME paints, as ROLES, and the theme that fills them,
// one per chrome vocabulary. THE PROGRAM'S COLORS ARE NO THEME'S since
// 2026-10-07: the waveform, the flags, the playhead's stem and the scanner
// are THE PALETTE's, a file type of its own (palette_file.h, whose head owns
// the rule that parts the two).
//
// A THEME IS A VALUE FOR EVERY CHROME ROLE. The roles are the role table
// below — THE ONE ENUMERATION of the chrome's: its name, the GuiPalette field
// it fills (render.h, whose palette block owns what each role paints) and
// WINDOWS 2000'S value. The roles are Windows 2000 Standard's recorded chrome
// (architect 2026-10-06, the Windows 2000 pivot; the catalog entry
// `windows-2000-standard`'s bytes, its setup hive's Control Panel\Colors:
// ButtonFace, ButtonText, the relief quartet, Hilight / HilightText, Window /
// WindowText; Windows' status bar's ButtonFace / ButtonText for the clock
// panel; Windows' InfoWindow / InfoText and its tooltip's black border for
// the cards; ActiveTitle / GradientActiveTitle / TitleText and InactiveTitle
// / GradientInactiveTitle / InactiveTitleText for the caption).
//
// EVERY CHROME'S THEME IS COMPILED IN AND THERE ARE NO THEME FILES (architect
// 2026-10-08, "okay to retire the color theme catalog"; "we just need
// hard-coded chromes"): no `theme` device key, no themes folder, no bundled
// files, no Settings row — THE CHROME CHOSEN IS ITS COLORS (kGuiChromeThemes
// below, resolved by the chrome's key, chrome_theme_words). The architect's
// workshop for colors is TWO KINDS OF NAMED PRESET, saved and loaded in the
// app (palette_file.h's head, the owner): A SCHEME — THE CHROME'S TWELVE
// KEYS and its face tag, `schemes/<name>.scheme` under the `scheme` device
// key — derived over this compiled theme by the live chrome's own
// derivation (under windows-2000 the 3D set from the ground by Windows' own
// rule, chrome_derive.h; the compiled words themselves never change), its
// BUILT-INS the catalog's entries transcribed to those keys (palette_file.h's
// kGuiChromeSchemes, generated); and A PALETTE — THE PROGRAM'S TWELVE
// ROLES, never this file's, `palettes/<name>.palette` under the `palette`
// key. A look made official becomes A NEW CHROME VARIANT, its theme compiled
// in beside this one:
//   `windows-2000` wears WINDOWS 2000's "Windows Standard" scheme
//     (tools/theme_catalog's `windows-2000-standard`): THE ROLE TABLE'S
//     VALUE COLUMN, hand-recorded, the generator checking it against its
//     catalog entry at every run.
// The imported catalog's other entries (Windows 95's and 98's schemes, the
// Plus! themes, GNOME's, KDE 3's, CDE's) stay in the catalog as the tool's
// RECORD;
// what ships of them is their twelve chrome keys alone, as the BUILT-IN
// SCHEMES (architect 2026-10-08 ~11:00; a preset kind of their own since the
// split of 2026-10-08, palette_file.h's head), no theme of theirs compiled
// here.
// EVERY BYTE HERE IS AN sRGB RECORD (architect 2026-10-08 ~05:15): Windows'
// scheme bytes are what those systems put into an sRGB frame buffer, as the captures show them; the tablet converts each at
// the painter's entry (display_transform.h's head), the laptop none.

// THE ROLE TABLE — THE ONE ENUMERATION OF THE CHROME'S ROLES (architect
// 2026-10-04), in GuiPalette's order: the Windows chrome's twenty-one (the
// caption's six since 2026-10-05). The program's roles left for the
// palette's own table 2026-10-07 (kGuiPaletteRoles, palette_file.h). The
// theme and install_palette (render.cpp) walk it, so a role cannot be valued
// and not painted. What each role paints is render.h's palette block (THE MAPPING).
struct GuiThemeRole {
    const char*          name;
    GuiColor GuiPalette::* member;
    uint32_t             windows_2000;   // 0xRRGGBB, Windows 2000's theme
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
    // GradientInactiveTitle / InactiveTitleText. Windows 2000's is its
    // navy-to-sky-blue caption under white and its grey-to-silver one under
    // the face's colour.
    {"caption_active",            &GuiPalette::caption_active,            0x0A246A},
    {"caption_active_gradient",   &GuiPalette::caption_active_gradient,   0xA6CAF0},
    {"caption_active_text",       &GuiPalette::caption_active_text,       0xFFFFFF},
    {"caption_inactive",          &GuiPalette::caption_inactive,          0x808080},
    {"caption_inactive_gradient", &GuiPalette::caption_inactive_gradient, 0xC0C0C0},
    {"caption_inactive_text",     &GuiPalette::caption_inactive_text,     0xD4D0C8},
};
inline constexpr std::size_t kGuiThemeRoleCount = std::size(kGuiThemeRoles);

// The index of the role named `name` in the table, or kGuiThemeRoleCount —
// the chrome knob's lookup (chrome_derive.h, evaluated at compile time).
constexpr std::size_t theme_role_index(std::string_view name) {
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        if (name == kGuiThemeRoles[i].name) return i;
    return kGuiThemeRoleCount;
}

// ONE THEME'S VALUES, as words in the role table's order — what
// install_palette reads.
using GuiThemeWords = std::array<uint32_t, kGuiThemeRoleCount>;

// WINDOWS 2000'S THEME: the role table's value column.
constexpr GuiThemeWords windows_2000_theme_words() {
    GuiThemeWords w{};
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        w[i] = kGuiThemeRoles[i].windows_2000;
    return w;
}
inline constexpr GuiThemeWords kGuiThemeWin2000 = windows_2000_theme_words();

// THE CHROMES' THEMES — each chrome's key and its compiled words, in the
// vocabularies' order (kGuiChromeSpecs; theme_file.cpp asserts that every
// chrome has one and that the two orders agree; palette_file.cpp that each
// chrome's own scheme is its theme key for key).
struct GuiChromeTheme {
    const char*          chrome;   // the ChromeSpec's key
    const GuiThemeWords* words;
};
inline constexpr GuiChromeTheme kGuiChromeThemes[] = {
    {"windows-2000", &kGuiThemeWin2000},
};

// THE ONE RESOLVER: the words of the chrome `spec` — read by install_palette
// (render.cpp) with the live chrome (live_chrome_spec()). Every chrome has a
// theme (theme_file.cpp's assert), so this never misses.
const GuiThemeWords& chrome_theme_words(const ChromeSpec& spec);

// THE ONE COLOUR GRAMMAR (architect 2026-10-03) — the palette files'
// (palette_file.h): `#` and six hexadecimal digits, either case, OR one of
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
// The word is the value's 0xRRGGBB, an sRGB triple (display_transform.h's
// head); nothing else is a colour.
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
