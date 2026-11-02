#pragma once

#include "theme_file.h"        // GuiThemeWords, the role table
#include "cool_edit_derive.h"  // the tones the chrome wears
#include "materials.h"         // the flat look's constants

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

// THE CHROME'S ROLES IN THE FLAT LOOK (architect 2026-11-01; materials.h's
// head). This header is THE ONE OWNER OF EVERY CHROME ROLE'S SOURCE: every
// role is A CONSTANT OF THE FLAT LOOK or ONE OF THE TWO RING TONES OFF THE
// FLAT GROUND (materials.h's kRingLight / kRingDark, which the overlays
// still wear until their part of the flat arc), installed at
// every install of the palette (render.cpp's install_palette). THE CAPTION
// HAS NO ROLE: the app paints none since 2026-10-24 (render.h's caption
// note).
//
// THE PER-ROLE TABLE (kChromeRoles below; the role table's 15, each decided
// once — the build proves every role is listed exactly once):
//   ground, light_3d                     = the flat ground kGround 242424
//   hilight                              = the ring's LIGHT tone
//   shadow, card_frame                   = the ring's DARK tone (the cards'
//                                          and the tooltip's ring, render.h's
//                                          card face)
//   dk_shadow                            = black
//   label, clock_text, card_text,
//   field_text                           = THE TEXT TONE kTextInk B5B5B5
//                                          (M13, 2026-11-02)
//   field_ground, clock_ground,
//   card_ground                          = THE FIELD kFieldGround 0A0A0C
//                                          (every field, the cards, the
//                                          tooltip, the modal box)
//   selected_fill                        = THE CONSTANT kSelectionFill,
//                                          637E86, Live's browser selection
//                                          (architect 2026-11-01 night, M5g /
//                                          M6)
//   selected_text                        = THE CONSTANT kSelectionText,
//                                          111111 (kTextOnLit)
namespace chrome_derive {

enum class Source {
    Ground, RingLight, RingDark, Black, Text, Field, SelectionFill,
    SelectionText,
};
struct ChromeRole {
    const char* role;
    Source      source;
};
inline constexpr ChromeRole kChromeRoles[] = {
    {"ground",                    Source::Ground},
    {"label",                     Source::Text},
    {"hilight",                   Source::RingLight},
    {"light_3d",                  Source::Ground},
    {"shadow",                    Source::RingDark},
    {"dk_shadow",                 Source::Black},
    {"selected_fill",             Source::SelectionFill},
    {"selected_text",             Source::SelectionText},
    {"field_ground",              Source::Field},
    {"field_text",                Source::Text},
    {"clock_ground",              Source::Field},
    {"clock_text",                Source::Text},
    {"card_ground",               Source::Field},
    {"card_text",                 Source::Text},
    {"card_frame",                Source::RingDark},
};

// Each listed role's index in the theme's role table, found once.
inline constexpr auto kChromeRoleIndex = [] {
    std::array<std::size_t, std::size(kChromeRoles)> a{};
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] = theme_role_index(kChromeRoles[i].role);
    return a;
}();

// EVERY CHROME ROLE IS DECIDED ONCE: each role of the table is listed
// exactly once and each listed name is a role — so a role added to the
// table is a build failure here until it is decided.
constexpr bool every_chrome_role_decided() {
    for (const std::size_t i : kChromeRoleIndex)
        if (i >= kGuiThemeRoleCount) return false;
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i) {
        int seen = 0;
        for (const std::size_t k : kChromeRoleIndex)
            if (k == i) ++seen;
        if (seen != 1) return false;
    }
    return true;
}
static_assert(every_chrome_role_decided());

// THE DERIVATION: every role off the flat look's constants.
constexpr GuiThemeWords derive_chrome() {
    GuiThemeWords w{};
    for (std::size_t i = 0; i < std::size(kChromeRoles); ++i) {
        uint32_t v = 0;
        switch (kChromeRoles[i].source) {
            case Source::Ground:        v = kGround;            break;
            case Source::RingLight:     v = kRingLight;         break;
            case Source::RingDark:      v = kRingDark;          break;
            case Source::Black:         v = 0x000000u;          break;
            case Source::Text:          v = kTextInk;           break;
            case Source::Field:         v = kFieldGround;       break;
            case Source::SelectionFill: v = kSelectionFill;     break;
            case Source::SelectionText: v = kSelectionText;     break;
        }
        w[kChromeRoleIndex[i]] = v & 0xFFFFFFu;
    }
    return w;
}

// -- THE CHECKS ----------------------------------------------------------------

// THE ROLES AS LISTED.
inline constexpr GuiThemeWords kDefaultCheck = derive_chrome();
static_assert(kDefaultCheck[theme_role_index("ground")] == 0x242424);
static_assert(kDefaultCheck[theme_role_index("hilight")] == kRingLight);
static_assert(kDefaultCheck[theme_role_index("shadow")] == kRingDark);
static_assert(kDefaultCheck[theme_role_index("dk_shadow")] == 0x000000);
static_assert(kDefaultCheck[theme_role_index("label")] == 0xB5B5B5);
static_assert(kDefaultCheck[theme_role_index("field_ground")] == 0x0A0A0C);
static_assert(kDefaultCheck[theme_role_index("field_text")] == 0xB5B5B5);
static_assert(kDefaultCheck[theme_role_index("selected_fill")] == 0x637E86);
static_assert(kDefaultCheck[theme_role_index("selected_text")] == 0x111111);
static_assert(kDefaultCheck[theme_role_index("card_ground")] == 0x0A0A0C);
static_assert(kDefaultCheck[theme_role_index("card_frame")] == kRingDark);
} // namespace chrome_derive

// THE CHROME'S LIVE WORDS — THE ONE RESOLVER the install reads
// (install_palette, render.cpp).
inline GuiThemeWords live_chrome_words() {
    return chrome_derive::derive_chrome();
}
