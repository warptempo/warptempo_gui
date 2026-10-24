#pragma once

#include "render.h"   // GuiPalette, GuiColor: the role table's members;
                      // through gui_font.h, chrome_spec.h's ChromeSpec

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// THE CHROME'S COLORS (architect 2026-10-04; COMPILED IN since 2026-10-08;
// INHERITED FROM COOL EDIT since 2026-10-10; OFF THE MATERIALS since
// 2026-10-21) — every color the flat CHROME paints, as ROLES. THE PROGRAM'S
// COLORS ARE NO THEME'S since 2026-10-07: the waveform, the flags, the
// playhead's stem and the scanner are THE PALETTE's (palette_file.h, whose
// head owns the rule that parts the two).
//
// THE CHROME INHERITS COOL EDIT (architect 2026-10-10: "the chrome color
// should just be the Cool Edit theme color … we're getting rid of chrome
// inside the body … even the font color, all of that would be inherited
// from Cool Edit"; "whatever luminous rules we previously had are
// superseded by the Cool Edit follow") — and SINCE 2026-10-23 THE HI-FI
// SKIN'S: every chrome role a constant of the skin or one of the two ring
// tones off the PANEL'S FACE, the brushed ground's mean (materials.h's TONE
// RULE; the per-role mapping is chrome_derive.h's, the one owner),
// re-derived at every install. THE CAPTION HAS NO ROLE: the app paints none
// since 2026-10-24 (render.h's caption note); its six roles — the active and inactive
// title's start, gradient end and text, a scheme's keys — were removed
// 2026-10-21 with the 42 built-in captions and the `caption` key
// (palette_file.h's head).
//
// THE ROLES are the role table below — THE ONE ENUMERATION of the chrome's:
// its name and the GuiPalette field it fills (render.h, whose palette block
// owns what each role paints). They are Windows' own element names
// (ButtonFace, ButtonText, the relief quartet, Hilight / HilightText, Window
// / WindowText, the status bar's pair, InfoWindow / InfoText and the
// tooltip's border), the chrome's painters written against them; the values
// are no longer Windows'.
// EVERY BYTE HERE IS AN sRGB RECORD (architect 2026-10-08 ~05:15), as the
// captures show them; the tablet converts each at the painter's entry
// (display_transform.h's head), the laptop none.

// THE ROLE TABLE — THE ONE ENUMERATION OF THE CHROME'S ROLES (architect
// 2026-10-04), in GuiPalette's order: the Windows chrome's fifteen (the
// caption's six, 2026-10-05..10-21, left with the captions). The program's
// roles left for the palette's own table 2026-10-07 (kGuiPaletteRoles,
// palette_file.h). The
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
