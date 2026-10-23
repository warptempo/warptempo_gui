#pragma once

#include "theme_file.h"        // GuiThemeWords, the role table
#include "palette_file.h"      // GuiPaletteWords, palette_role_index, the
                               // default palette (the checks)
#include "cool_edit_derive.h"  // the tones the chrome wears
#include "materials.h"         // the selection pair's constants

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

// THE CHROME'S ROLES IN THE HI-FI PANEL SKIN (architect 2026-10-23;
// materials.h's head). This header is THE ONE OWNER OF EVERY CHROME ROLE'S
// SOURCE: every role that was a tone of the panel's Face (Cool Edit's
// arithmetic, 2026-10-10 to 2026-10-23 — "the chrome inherits Cool Edit")
// is A CONSTANT OF THE SKIN or ONE OF THE TWO RING TONES OFF THE GROUND'S
// MEAN (materials.h's TONE RULE; render.cpp's panel_face hands the mean in as
// the words' `face`), re-derived at every install of the palette
// (render.cpp's install family). The palette's own `face` (Cool Edit's
// preset text) is no source; the generated presets keep it for the
// static_asserts. THE CAPTION HAS NO ROLE: its ground and inks are render.h's
// caption block's constants.
//
// THE PER-ROLE TABLE (kChromeRoles below; the role table's 15, each decided
// once — the build proves every role is listed exactly once):
//   ground, light_3d                     = the Face, the ground's mean
//   hilight                              = the ring's LIGHT tone
//   shadow, card_frame                   = the ring's DARK tone (the cards'
//                                          and the tooltip's ring, render.h's
//                                          card face)
//   dk_shadow                            = black
//   label, clock_text, card_text,
//   field_text                           = THE BONE kBoneInk
//   field_ground, clock_ground,
//   card_ground                          = THE FIELD kFieldGround 0A0A0C
//                                          (every field, the cards, the
//                                          tooltip, the modal box)
//   selected_fill                        = THE CONSTANT kSelectionFill,
//                                          3E4044, the grey bar (architect
//                                          2026-10-23: "the grey everywhere";
//                                          the brass 9C7B3A of 2026-10-21
//                                          retired)
//   selected_text                        = THE CONSTANT kSelectionText, the
//                                          bone
namespace chrome_derive {

enum class Source {
    Face, RingLight, RingDark, Black, Bone, Field, SelectionFill,
    SelectionText,
};
struct ChromeRole {
    const char* role;
    Source      source;
};
inline constexpr ChromeRole kChromeRoles[] = {
    {"ground",                    Source::Face},
    {"label",                     Source::Bone},
    {"hilight",                   Source::RingLight},
    {"light_3d",                  Source::Face},
    {"shadow",                    Source::RingDark},
    {"dk_shadow",                 Source::Black},
    {"selected_fill",             Source::SelectionFill},
    {"selected_text",             Source::SelectionText},
    {"field_ground",              Source::Field},
    {"field_text",                Source::Bone},
    {"clock_ground",              Source::Field},
    {"clock_text",                Source::Bone},
    {"card_ground",               Source::Field},
    {"card_text",                 Source::Bone},
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

// THE DERIVATION: every role off the words' `face` (the ground's mean) and
// the skin's constants.
constexpr GuiThemeWords derive_chrome(const GuiPaletteWords& program) {
    constexpr std::size_t kFace = palette_role_index("face");
    static_assert(kFace < kGuiPaletteRoleCount);
    const uint32_t face = program[kFace] & 0xFFFFFFu;
    GuiThemeWords w{};
    for (std::size_t i = 0; i < std::size(kChromeRoles); ++i) {
        uint32_t v = 0;
        switch (kChromeRoles[i].source) {
            case Source::Face:          v = face;               break;
            case Source::RingLight:     v = ring_light_of(face); break;
            case Source::RingDark:      v = ring_dark_of(face);  break;
            case Source::Black:         v = 0x000000u;          break;
            case Source::Bone:          v = kBoneInk;           break;
            case Source::Field:         v = kFieldGround;       break;
            case Source::SelectionFill: v = kSelectionFill;     break;
            case Source::SelectionText: v = kSelectionText;     break;
        }
        w[kChromeRoleIndex[i]] = v & 0xFFFFFFu;
    }
    return w;
}

// -- THE CHECKS ----------------------------------------------------------------

// THE SKIN ON THE SHIPPED GROUND'S MEAN OF RECORD (make_pivot1, about
// 1F1F21): the rings about 48484B and 0C0C0D, the constants as listed.
inline constexpr GuiThemeWords kDefaultCheck = [] {
    GuiPaletteWords p = default_palette_words();
    p[palette_role_index("face")] = 0x1F1F21;
    return derive_chrome(p);
}();
static_assert(kDefaultCheck[theme_role_index("ground")] == 0x1F1F21);
static_assert(kDefaultCheck[theme_role_index("hilight")] == ring_light_of(0x1F1F21));
static_assert(kDefaultCheck[theme_role_index("shadow")] == ring_dark_of(0x1F1F21));
static_assert(kDefaultCheck[theme_role_index("dk_shadow")] == 0x000000);
static_assert(kDefaultCheck[theme_role_index("label")] == 0xF0F0F0);
static_assert(kDefaultCheck[theme_role_index("field_ground")] == 0x0A0A0C);
static_assert(kDefaultCheck[theme_role_index("field_text")] == 0xF0F0F0);
static_assert(kDefaultCheck[theme_role_index("selected_fill")] == 0x3E4044);
static_assert(kDefaultCheck[theme_role_index("selected_text")] == 0xF0F0F0);
static_assert(kDefaultCheck[theme_role_index("card_ground")] == 0x0A0A0C);
static_assert(kDefaultCheck[theme_role_index("card_frame")] == ring_dark_of(0x1F1F21));
// The ring tones of record (make_pivot1.ring_tones on 1F1F21), each within a
// byte of the mock's 48484B / 0C0C0D.
static_assert(cool_edit_derive::within_two(ring_light_of(0x1F1F21), 0x48484B));
static_assert(cool_edit_derive::within_two(ring_dark_of(0x1F1F21), 0x0C0C0D));
} // namespace chrome_derive

// THE CHROME'S LIVE WORDS — THE ONE RESOLVER the install family reads
// (install_palette / install_program_palette, render.cpp): the derivation
// over the words it hands in, their `face` the panel's (render.cpp's
// panel_face).
inline GuiThemeWords live_chrome_words(const GuiPaletteWords& program) {
    return chrome_derive::derive_chrome(program);
}
