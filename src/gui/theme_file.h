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
// it, the waveform's ink, and the program text's two members, the selection
// taking the other one (cool_edit_derive.h's tones; the per-role mapping is
// chrome_derive.h's, the one owner) — re-derived at every install of the
// palette, so a new Face or ink moves the chrome live. THE CAPTION'S SIX
// ARE A SCHEME'S (palette_file.h's head: a scheme is the caption alone since
// 2026-10-10), or, with no scheme keys live, the live chrome's COMPILED
// CAPTION below.
//
// THE ROLES are the role table below — THE ONE ENUMERATION of the chrome's:
// its name and the GuiPalette field it fills (render.h, whose palette block
// owns what each role paints). They are Windows' own element names
// (ButtonFace, ButtonText, the relief quartet, Hilight / HilightText, Window
// / WindowText, the status bar's pair, InfoWindow / InfoText and the
// tooltip's border, and the six of the caption), the chrome's painters
// written against them; the values are no longer Windows'.
//
// THE COMPILED CAPTION (chrome_caption, palette_file.h): the caption the
// default `caption=default` paints, the six caption keys of the chrome's own
// scheme (chrome_spec.h's own_scheme), which since architect 2026-10-21 is
// the product's own `default` — 9B4B2C flat under FFFFFF, its inactive
// three following (Windows Storm 2026-10-12..21). It is read straight from
// the built-in table (kGuiChromeSchemes, whose first row it is, hand-written
// beside the generated chrome_schemes.inc), never retyped.
// The imported catalog's entries stay in the catalog as the tool's RECORD;
// what ships of them is their caption keys alone, as the generated BUILT-IN
// SCHEMES after the product's own (palette_file.h's kGuiChromeSchemes).
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

// (THE ONE COLOUR GRAMMAR — `#rrggbb` or one of Windows' twenty always-solid
// colours by name — was the theme files' and then the preset files' value
// grammar, 2026-10-03..10-14, and left with the preset files, architect
// 2026-10-14: every color is compiled in.)
