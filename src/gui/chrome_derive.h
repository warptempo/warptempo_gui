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

// THE CHROME INHERITS COOL EDIT (architect 2026-10-10 ~10:20–11:30: "the
// chrome color should just be the Cool Edit theme color … we're getting rid
// of chrome inside the body … even the font color, all of that would be
// inherited from Cool Edit"; "whatever luminous rules we previously had are
// superseded by the Cool Edit follow. We follow Cool Edit's theming"). This
// header is THE ONE OWNER OF EVERY CHROME ROLE'S SOURCE: every role is A
// TONE OF THE PANEL'S FACE — the brass's mean since 2026-10-21 (materials.h's
// TONE RULE; render.cpp's panel_face hands it in as the words' `face`) — by
// Cool Edit's own arithmetic (cool_edit_derive.h, whose head has the
// measurements and the proofs), re-derived at every install of the palette
// (render.cpp's install family) — but the selection pair, a constant since
// 2026-10-21 (no role reads the waveform's ink: the 2026-10-10 selection
// fill did, until then). THE CAPTION
// HAS NO ROLE since 2026-10-21: its ground is a flat constant and its
// buttons Windows 2000's Standard scheme since 2026-10-22 (render.h's
// caption block; the walnut 2026-10-21 to 2026-10-22; its six roles were a
// scheme's 2026-10-05..10-21).
//
// THE PER-ROLE TABLE (kChromeRoles below; the role table's 15, each decided
// once — the build proves every role is listed exactly once):
//   ground, light_3d, clock_ground       = the Face (Cool Edit's "Dockable
//                                          Window 3D Color"; 3DLight the face,
//                                          as Windows' own schemes record it)
//   hilight                              = the `hilight` tone (Cool Edit's
//                                          Hilight: a pane's top and left
//                                          line)
//   shadow                               = the `mid` tone (a pane's bottom
//                                          and right line)
//   dk_shadow                            = black (Cool Edit's case outline,
//                                          000000 on all nine captured Faces)
//   label, clock_text, card_text         = the program text WITH ITS SWAP
//                                          (cool_edit_derive::program_text:
//                                          light on a dark Face, dark past
//                                          the text threshold)
//   field_ground                         = the `mid` tone (Cool Edit's dark
//                                          time field)
//   field_text                           = the light text that NEVER swaps
//                                          (cool_edit_derive::field_text: the
//                                          field's ground is dark at every
//                                          Face)
//   selected_fill                        = THE CONSTANT kSelectionFill,
//                                          9C7B3A, brass-like (materials.h;
//                                          architect 2026-10-21)
//   selected_text                        = THE CONSTANT kSelectionText, the
//                                          near-white (architect 2026-10-21:
//                                          "the selection text white or
//                                          near-white"); the waveform's ink
//                                          under the program text's other
//                                          member (2026-10-10 / 10-16) and
//                                          the caption's pair (7e0b8d4e, one
//                                          landing) before it, retired
//                                          (closed_questions.md)
//   card_ground                          = the Face
//   card_frame                           = the `dark` tone (Cool Edit's box
//                                          outline and the label's shadow)
// THE CARDS AND THE TOOLTIP take the Face, the derived text and the dark
// outline (architect 2026-10-10), Windows' cream InfoWindow under black
// retired. Windows' Appearance-dialog rule that derived the 3D set from a
// picked ground retired the same day with the ground key
// (closed_questions.md).
namespace chrome_derive {

enum class Source {
    Face, Hilight, Mid, Dark, Black, Text, FieldText, SelectionFill,
    SelectionText,
};
struct ChromeRole {
    const char* role;
    Source      source;
};
inline constexpr ChromeRole kChromeRoles[] = {
    {"ground",                    Source::Face},
    {"label",                     Source::Text},
    {"hilight",                   Source::Hilight},
    {"light_3d",                  Source::Face},
    {"shadow",                    Source::Mid},
    {"dk_shadow",                 Source::Black},
    {"selected_fill",             Source::SelectionFill},
    {"selected_text",             Source::SelectionText},
    {"field_ground",              Source::Mid},
    {"field_text",                Source::FieldText},
    {"clock_ground",              Source::Face},
    {"clock_text",                Source::Text},
    {"card_ground",               Source::Face},
    {"card_text",                 Source::Text},
    {"card_frame",                Source::Dark},
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

// THE DERIVATION: every role off the program's words (their `face` the
// panel's, their ink the waveform's, through Cool Edit's tones).
constexpr GuiThemeWords derive_chrome(const GuiPaletteWords& program) {
    using namespace cool_edit_derive;
    constexpr std::size_t kFace = palette_role_index("face");
    constexpr std::size_t kInk = palette_role_index("waveform_ink");
    static_assert(kFace < kGuiPaletteRoleCount && kInk < kGuiPaletteRoleCount);
    const uint32_t face = program[kFace] & 0xFFFFFFu;
    const uint32_t ink = program[kInk] & 0xFFFFFFu;
    GuiThemeWords w{};
    for (std::size_t i = 0; i < std::size(kChromeRoles); ++i) {
        uint32_t v = 0;
        switch (kChromeRoles[i].source) {
            case Source::Face:       v = face;                                  break;
            case Source::Hilight:    v = tone_of(named_tone("hilight"), face, ink); break;
            case Source::Mid:        v = tone_of(named_tone("mid"), face, ink); break;
            case Source::Dark:       v = tone_of(named_tone("dark"), face, ink); break;
            case Source::Black:      v = 0x000000u;                             break;
            case Source::Text:       v = program_text(face);                    break;
            case Source::FieldText:  v = field_text(face);                      break;
            case Source::SelectionFill: v = kSelectionFill;                     break;
            case Source::SelectionText: v = kSelectionText;                     break;
        }
        w[kChromeRoleIndex[i]] = v & 0xFFFFFFu;
    }
    return w;
}

// -- THE CHECKS ----------------------------------------------------------------

// THE DEFAULT PALETTE (Cool Edit's "(Default Scheme)": Face 626C7B, ink
// 4BF3A7): the chrome it derives, role by role — the rule's proof on Cool
// Edit's own Face (the live Face is the brass's, measured at the bake).
inline constexpr GuiThemeWords kDefaultCheck =
    derive_chrome(default_palette_words());
static_assert(kDefaultCheck[theme_role_index("ground")] == 0x626C7B);
static_assert(kDefaultCheck[theme_role_index("light_3d")] == 0x626C7B);
static_assert(kDefaultCheck[theme_role_index("hilight")] == 0xAEB4BE);
static_assert(kDefaultCheck[theme_role_index("shadow")] == 0x404750);
static_assert(kDefaultCheck[theme_role_index("dk_shadow")] == 0x000000);
static_assert(kDefaultCheck[theme_role_index("label")] == 0xEFEFEF);
static_assert(kDefaultCheck[theme_role_index("field_ground")] == 0x404750);
static_assert(kDefaultCheck[theme_role_index("field_text")] == 0xEFEFEF);
static_assert(kDefaultCheck[theme_role_index("selected_fill")] == 0x9C7B3A);
static_assert(kDefaultCheck[theme_role_index("selected_text")] == 0xF0F0F0);
static_assert(kDefaultCheck[theme_role_index("card_ground")] == 0x626C7B);
static_assert(kDefaultCheck[theme_role_index("card_text")] == 0xEFEFEF);
static_assert(kDefaultCheck[theme_role_index("card_frame")] == 0x2A2F35);
// A LIGHT FACE SWAPS THE TEXT AND NOT THE FIELD'S (Arctic Freeze's C0C0C0,
// METRICS_PRESETS_1010.md §3a): the label dark, the field text light — and
// the selection pair the constants whatever the Face (2026-10-21).
static_assert([] {
    GuiPaletteWords p = default_palette_words();
    p[palette_role_index("face")] = 0xC0C0C0;
    const GuiThemeWords w = derive_chrome(p);
    return w[theme_role_index("label")] == 0x311919 &&
           w[theme_role_index("card_text")] == 0x311919 &&
           w[theme_role_index("field_text")] == 0xF6F6F6 &&
           w[theme_role_index("field_ground")] == 0x7F7F7F &&
           w[theme_role_index("selected_text")] == 0xF0F0F0;
}());
} // namespace chrome_derive

// THE CHROME'S LIVE WORDS — THE ONE RESOLVER the install family reads
// (install_palette / install_program_palette, render.cpp): the derivation
// over the words it hands in, their `face` the panel's (render.cpp's
// panel_face).
inline GuiThemeWords live_chrome_words(const GuiPaletteWords& program) {
    return chrome_derive::derive_chrome(program);
}
