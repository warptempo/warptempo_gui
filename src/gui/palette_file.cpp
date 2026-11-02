#include "palette_file.h"

#include "theme_file.h"        // kGuiThemeRoles (the chrome's members)
#include "cool_edit_derive.h"  // the Cool Edit block's tones: the coverage check

#include <array>
#include <iterator>

namespace {

// THE TWO TABLES COVER GuiPalette EXACTLY (install_palette fills it off
// both, render.cpp): the program's members are distinct, none of them is a
// chrome role's member, and the counts sum to the struct's members — so no
// member is filled twice or left at construction state.
constexpr bool palette_members_disjoint() {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i) {
        for (std::size_t j = i + 1; j < kGuiPaletteRoleCount; ++j)
            if (kGuiPaletteRoles[i].member == kGuiPaletteRoles[j].member)
                return false;
        if (kGuiPaletteRoles[i].member == &GuiPalette::face) return false;
        for (const GuiThemeRole& t : kGuiThemeRoles)
            if (t.member == kGuiPaletteRoles[i].member) return false;
    }
    return true;
}
static_assert(palette_members_disjoint());
// THE COOL EDIT BLOCK'S DERIVED TONES (cool_edit_derive.h, filled off the
// flat ground, render.cpp) are the struct's third part — the table's
// painted tones: distinct, and neither a chrome role's member nor a program
// role's nor the panel's `face` — and the disabled cue's two faded colors
// (kDisabledCueMemberCount) after them.
constexpr bool cool_edit_tones_disjoint() {
    std::array<GuiColor GuiPalette::*, cool_edit_derive::kPaintedToneCount>
        members{};
    std::size_t n = 0;
    for (const cool_edit_derive::Tone& t : cool_edit_derive::kTones)
        if (t.member != nullptr) members[n++] = t.member;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j)
            if (members[j] == members[i]) return false;
        if (members[i] == &GuiPalette::face) return false;
        for (const GuiThemeRole& t : kGuiThemeRoles)
            if (t.member == members[i]) return false;
        for (const GuiPaletteRole& p : kGuiPaletteRoles)
            if (p.member == members[i]) return false;
    }
    return true;
}
static_assert(cool_edit_tones_disjoint());
// The struct's members: the chrome's roles, the program's roles, the
// panel's `face` (the flat ground, one member of its own), the Cool Edit
// block's painted tones, and the disabled cue's two.
static_assert(sizeof(GuiPalette) ==
              (kGuiThemeRoleCount + kGuiPaletteRoleCount + 1 +
               cool_edit_derive::kPaintedToneCount +
               cool_edit_derive::kDisabledCueMemberCount) *
                  sizeof(GuiColor));

} // namespace
