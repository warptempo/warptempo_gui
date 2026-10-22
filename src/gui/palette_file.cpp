#include "palette_file.h"

#include "theme_file.h"        // kGuiThemeRoles (the chrome's members)
#include "cool_edit_derive.h"  // the Cool Edit block's tones: the coverage check

#include <algorithm>
#include <array>
#include <cassert>
#include <iterator>
#include <string_view>

namespace {

// The role table names each role once, so the lookup (palette_role_index) is a
// bijection (eleven names: the pairwise form is cheap here).
constexpr bool palette_role_names_unique() {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        for (std::size_t j = i + 1; j < kGuiPaletteRoleCount; ++j)
            if (std::string_view(kGuiPaletteRoles[i].name) ==
                kGuiPaletteRoles[j].name)
                return false;
    return true;
}
static_assert(palette_role_names_unique());
// THE TWO TABLES COVER GuiPalette EXACTLY (install_palette fills it off
// both, render.cpp): the program's members are distinct, none of them is a
// chrome role's member, and the two counts sum to the struct's members — so
// no member is filled twice or left at construction state.
constexpr bool palette_members_disjoint() {
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i) {
        for (std::size_t j = i + 1; j < kGuiPaletteRoleCount; ++j)
            if (kGuiPaletteRoles[i].member == kGuiPaletteRoles[j].member)
                return false;
        for (const GuiThemeRole& t : kGuiThemeRoles)
            if (t.member == kGuiPaletteRoles[i].member) return false;
    }
    return true;
}
static_assert(palette_members_disjoint());
// THE COOL EDIT BLOCK'S DERIVED TONES (cool_edit_derive.h, filled with the
// program's words, render.cpp) are the struct's third part — the table's
// painted tones and the panel label's swapping pair: distinct, and neither a
// chrome role's member nor a program role's — and the disabled cue's four
// faded colors (kDisabledCueMemberCount) after them. (The button case's
// twenty-four members and the tab strip's seventeen, 2026-10-10 and
// 2026-10-18, left 2026-10-21 with the materials, materials.h.)
constexpr bool cool_edit_tones_disjoint() {
    std::array<GuiColor GuiPalette::*,
               cool_edit_derive::kPaintedToneCount +
                   std::size(cool_edit_derive::kLabelPairMembers)>
        members{};
    std::size_t n = 0;
    for (const cool_edit_derive::Tone& t : cool_edit_derive::kTones)
        if (t.member != nullptr) members[n++] = t.member;
    for (GuiColor GuiPalette::* m : cool_edit_derive::kLabelPairMembers)
        members[n++] = m;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j)
            if (members[j] == members[i]) return false;
        for (const GuiThemeRole& t : kGuiThemeRoles)
            if (t.member == members[i]) return false;
        for (const GuiPaletteRole& p : kGuiPaletteRoles)
            if (p.member == members[i]) return false;
    }
    return true;
}
static_assert(cool_edit_tones_disjoint());
static_assert(sizeof(GuiPalette) ==
              (kGuiThemeRoleCount + kGuiPaletteRoleCount +
               cool_edit_derive::kPaintedToneCount +
               std::size(cool_edit_derive::kLabelPairMembers) +
               cool_edit_derive::kDisabledCueMemberCount) *
                  sizeof(GuiColor));
// EVERY BUILT-IN PALETTE'S FACE HAS A ROW IN THE TAB TABLE (2026-10-18,
// cool_edit_derive.h's kTabBodies: the inactive tab's body and label are each
// preset's measured bytes, no rule) — the table's completeness as a record;
// the install reads Default's row alone since 2026-10-21 (the inactive
// tab's gradient on its material, the wood's since 2026-10-22, materials.h's
// WOOD), so the lookup can never miss.
static_assert(std::ranges::all_of(kGuiBuiltinPalettes,
                                  [](const GuiBuiltinPalette& b) {
    const uint32_t face = b.words[palette_role_index("face")];
    return std::ranges::any_of(cool_edit_derive::kTabBodies,
                               [face](const cool_edit_derive::TabBody& t) {
                                   return t.face == face;
                               });
}));

// THE KEY GRAMMAR OF A BUILT-IN (the hyphen grammar): one or more of lowercase ASCII letters, digits and '-' — a value the
// device config writes and reads verbatim.
constexpr bool is_preset_key_spelling(std::string_view v) {
    if (v.empty()) return false;
    for (const char c : v)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    return true;
}

// THE BUILT-IN PALETTES ARE WELL FORMED (palette_file.h's
// kGuiBuiltinPalettes; the generator checks the same): the keys and display
// names unique, the keys in the key grammar, THE DEFAULT A ROW OF THE TABLE.
constexpr bool builtin_palettes_well_formed() {
    if (builtin_palette(kGuiDefaultPaletteKey) == nullptr) return false;
    for (std::size_t i = 0; i < std::size(kGuiBuiltinPalettes); ++i) {
        const GuiBuiltinPalette& a = kGuiBuiltinPalettes[i];
        if (!is_preset_key_spelling(a.key)) return false;
        for (std::size_t j = i + 1; j < std::size(kGuiBuiltinPalettes); ++j)
            if (std::string_view(a.key) == kGuiBuiltinPalettes[j].key ||
                std::string_view(a.display_name) ==
                    kGuiBuiltinPalettes[j].display_name)
                return false;
    }
    return true;
}
static_assert(builtin_palettes_well_formed());
// EVERY BUILT-IN'S LIT OUTLINE IS ITS INK AT THE VIEW BAR SPAN'S SHADOW
// LIGHTNESS (palette_file.h's role table: Cool Edit records no outline; the
// generator ran cool_edit_derive.h's round trip once, and the compiled bytes
// are the record — this proves the record, nothing derives at paint time).
static_assert(std::ranges::all_of(kGuiBuiltinPalettes,
                                  [](const GuiBuiltinPalette& b) {
    const cool_edit_derive::Tone& lo = cool_edit_derive::ink_tone("span_shadow");
    return b.words[palette_role_index("waveform_outline")] ==
           cool_edit_derive::tone(b.words[palette_role_index("waveform_ink")],
                                  lo.a, lo.b);
}));
// EVERY BUILT-IN'S INVALID COLOR IS ITS CENTER LINE (palette_file.h's role
// table, architect 2026-10-11; the generator copies Cool Edit's Cntr into
// both, and this proves the record).
static_assert(std::ranges::all_of(kGuiBuiltinPalettes,
                                  [](const GuiBuiltinPalette& b) {
    return b.words[palette_role_index("invalid_label")] ==
           b.words[palette_role_index("center")];
}));
// (THE CHOICE ROWS' ORDERS' PROOFS — each a permutation of its table, its
// first row the kind's default — went with the orders, 2026-10-14.)

} // namespace

GuiPaletteWords palette_record(std::string_view name) {
    const GuiBuiltinPalette* b = builtin_palette(name);
    // Every caller's name came through is_builtin_palette_name: a miss is a
    // program bug.
    assert(b != nullptr);
    return b->words;
}
