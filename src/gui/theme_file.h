#pragma once

#include "render.h"   // GuiPalette, GuiColor: the role table's members;
                      // through gui_font.h, chrome_spec.h's ChromeSpec

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// THE CHROME'S COLORS (architect 2026-10-04; COMPILED IN since 2026-10-08;
// INHERITED FROM COOL EDIT since 2026-10-10) — every color the CHROME
// paints, as ROLES. THE PROGRAM'S COLORS ARE NO THEME'S since 2026-10-07:
// the waveform, the flags, the playhead's stem, the scanner and the panel's
// face are THE PALETTE's, a file type of its own (palette_file.h, whose head
// owns the rule that parts the two).
//
// THE CHROME INHERITS COOL EDIT (architect 2026-10-10: "the chrome color
// should just be the Cool Edit theme color … we're getting rid of chrome
// inside the body. The caption remains the only thing that's outside of Cool
// Edit … even the font color, all of that would be inherited from Cool
// Edit"; "whatever luminous rules we previously had are superseded by the
// Cool Edit follow"): EVERY CHROME ROLE BUT THE CAPTION'S SIX IS A TONE OF
// THE LIVE PALETTE — the panel's `face` and the tones Cool Edit derives from
// it, the waveform's ink and its darkened text (cool_edit_derive.h's tones;
// the per-role mapping is chrome_derive.h's, the one owner) — re-derived at
// every install of the palette, so a pick of the Face or the ink moves the
// chrome live. THE CAPTION'S SIX ARE A SCHEME'S (palette_file.h's head: a
// scheme is the caption alone since 2026-10-10), or, with no scheme keys
// live, the live chrome's COMPILED CAPTION below.
//
// THE ROLES are the role table below — THE ONE ENUMERATION of the chrome's:
// its name and the GuiPalette field it fills (render.h, whose palette block
// owns what each role paints). They are Windows' own element names
// (ButtonFace, ButtonText, the relief quartet, Hilight / HilightText, Window
// / WindowText, the status bar's pair, InfoWindow / InfoText and the
// tooltip's border, and the six of the caption), the chrome's painters
// written against them; the values are no longer Windows'.
//
// THE COMPILED CAPTION (kGuiCaptionWin2000 below, read through
// chrome_caption): the caption a config with no `scheme` line paints, the six
// caption keys of the chrome's own scheme (chrome_spec.h's own_scheme).
// Windows 2000 is the one chrome (architect 2026-10-10) and wears its
// "Windows Standard" caption (tools/theme_catalog's `windows-2000-standard`,
// its setup hive's ActiveTitle / GradientActiveTitle / TitleText and
// InactiveTitle / GradientInactiveTitle / InactiveTitleText):
// HAND-RECORDED below, the generator checking it against its catalog entry
// at every run.
// The imported catalog's other entries stay in the catalog as the tool's
// RECORD; what ships of them is their caption keys alone, as the BUILT-IN
// SCHEMES (palette_file.h's kGuiChromeSchemes, generated).
// EVERY BYTE HERE IS AN sRGB RECORD (architect 2026-10-08 ~05:15): Windows'
// scheme bytes are what those systems put into an sRGB frame buffer, as the
// captures show them; the tablet converts each at the painter's entry
// (display_transform.h's head), the laptop none.

// THE ROLE TABLE — THE ONE ENUMERATION OF THE CHROME'S ROLES (architect
// 2026-10-04), in GuiPalette's order: the Windows chrome's twenty-one (the
// caption's six since 2026-10-05). The program's roles left for the
// palette's own table 2026-10-07 (kGuiPaletteRoles, palette_file.h). The
// derivation (chrome_derive.h, which decides every role exactly once) and
// install_palette (render.cpp) walk it, so a role cannot be valued and not
// painted. What each role paints is render.h's palette block (THE MAPPING).
struct GuiThemeRole {
    const char*            name;
    GuiColor GuiPalette::* member;
};
inline constexpr GuiThemeRole kGuiThemeRoles[] = {
    // THE CHROME — the live palette's tones (chrome_derive.h).
    {"ground",                    &GuiPalette::ground},
    {"label",                     &GuiPalette::label},
    {"hilight",                   &GuiPalette::hilight},
    {"light_3d",                  &GuiPalette::light_3d},
    {"shadow",                    &GuiPalette::shadow},
    {"dk_shadow",                 &GuiPalette::dk_shadow},
    {"selected_fill",             &GuiPalette::selected_fill},
    {"selected_text",             &GuiPalette::selected_text},
    {"field_ground",              &GuiPalette::field_ground},
    {"field_text",                &GuiPalette::field_text},
    {"clock_ground",              &GuiPalette::clock_ground},
    {"clock_text",                &GuiPalette::clock_text},
    {"card_ground",               &GuiPalette::card_ground},
    {"card_text",                 &GuiPalette::card_text},
    {"card_frame",                &GuiPalette::card_frame},
    // THE CAPTION (architect 2026-10-05): the window's title bar, ACTIVE
    // while the window has the focus and INACTIVE without it (the tablet's is
    // always active, GuiPlatform::caption_active), each a START at the left,
    // a GRADIENT END at the right and the title's TEXT — Windows' ActiveTitle
    // / GradientActiveTitle / TitleText and InactiveTitle /
    // GradientInactiveTitle / InactiveTitleText; the scheme's six keys, the
    // one part of the chrome outside Cool Edit (2026-10-10).
    {"caption_active",            &GuiPalette::caption_active},
    {"caption_active_gradient",   &GuiPalette::caption_active_gradient},
    {"caption_active_text",       &GuiPalette::caption_active_text},
    {"caption_inactive",          &GuiPalette::caption_inactive},
    {"caption_inactive_gradient", &GuiPalette::caption_inactive_gradient},
    {"caption_inactive_text",     &GuiPalette::caption_inactive_text},
};
inline constexpr std::size_t kGuiThemeRoleCount = std::size(kGuiThemeRoles);

// The index of the role named `name` in the table, or kGuiThemeRoleCount —
// the derivation's lookup (chrome_derive.h, evaluated at compile time).
constexpr std::size_t theme_role_index(std::string_view name) {
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        if (name == kGuiThemeRoles[i].name) return i;
    return kGuiThemeRoleCount;
}

// THE CHROME'S WORDS, in the role table's order — what the derivation
// answers and install_palette reads.
using GuiThemeWords = std::array<uint32_t, kGuiThemeRoleCount>;

// WINDOWS 2000'S COMPILED CAPTION (the head): Windows Standard's navy-to-sky-
// blue caption under white and its grey-to-silver one under the face's
// D4D0C8 — the six in GuiChromePick's order (render.h), the inactive three
// recorded. tools/theme_catalog/gen_theme_files.py reads this initializer
// and checks it against the catalog entry.
inline constexpr GuiChromePick kGuiCaptionWin2000{
    0x0A246A, 0xA6CAF0, 0xFFFFFF, 0x808080, 0xC0C0C0, 0xD4D0C8};

// THE ONE RESOLVER: the compiled caption — read by the derivation's live
// words (live_chrome_words, chrome_derive.h) and the picker's OLD while no
// scheme keys stand. palette_file.cpp asserts that the chrome's own scheme is
// this caption key for key.
inline constexpr const GuiChromePick& chrome_caption() {
    return kGuiCaptionWin2000;
}

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
