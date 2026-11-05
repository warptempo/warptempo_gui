#include "palette_file.h"

#include "theme_file.h"        // kGuiThemeRoles (the chrome's members)

#include <iterator>

namespace {

// THE TWO TABLES COVER GuiPalette EXACTLY (install_palette fills it off
// both, render.cpp): the program's members are distinct, none of them is a
// chrome role's member or the panel's `face`, every program role names its
// tone, and the counts sum to the struct's members — so no member is filled
// twice or left at construction state (the chrome's own distinctness is
// theme_file.h's every_chrome_role_decided).
constexpr bool palette_members_disjoint() {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i) {
        if (kGuiPaletteRoles[i].tone == nullptr) return false;
        for (std::size_t j = i + 1; j < kGuiPaletteRoleCount; ++j)
            if (kGuiPaletteRoles[i].member == kGuiPaletteRoles[j].member)
                return false;
        if (kGuiPaletteRoles[i].member == &GuiPalette::face) return false;
        for (const GuiThemeRole& t : kGuiThemeRoles)
            if (t.member == kGuiPaletteRoles[i].member) return false;
    }
    for (const GuiThemeRole& t : kGuiThemeRoles)
        if (t.member == &GuiPalette::face) return false;
    return true;
}
static_assert(palette_members_disjoint());
// The struct's members: the chrome's roles, the program's roles, and the
// panel's `face` (the ground's own member, filled off the theme's ground).
static_assert(sizeof(GuiPalette) ==
              (kGuiThemeRoleCount + kGuiPaletteRoleCount + 1) *
                  sizeof(GuiColor));

} // namespace
